#pragma once
#include "clipper2next/api/error.h"
#include <array>
#include <cstddef>
#include <type_traits>
#include <algorithm>
#include <limits>
#include <utility>
#include <cassert>
#include "support/private/parallel_workspace_observer.h"

#if defined(_MSC_VER)
#define CLIPPER2NEXT_RESOURCE_FORCE_INLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#define CLIPPER2NEXT_RESOURCE_FORCE_INLINE inline __attribute__((always_inline))
#else
#define CLIPPER2NEXT_RESOURCE_FORCE_INLINE inline
#endif

namespace clipper2next::internal {
enum class workspace_category { engine, staging };

struct execution_resource_ledger {
    execution_resource_ledger() = default;
    execution_resource_ledger(const execution_resource_ledger&) = delete;
    auto operator=(const execution_resource_ledger&) -> execution_resource_ledger& = delete;
    execution_resource_ledger(execution_resource_ledger&&) = delete;
    auto operator=(execution_resource_ledger&&) -> execution_resource_ledger& = delete;
    auto reset_usage() noexcept -> void {
        assert(engine_bytes == 0U && staging_bytes == 0U && reserved_engine_bytes == 0U && reserved_staging_bytes == 0U);
        engine_high_water = 0U;
        staging_high_water = 0U;
        work = 0U;
    }
    std::size_t maximum_work{(std::numeric_limits<std::size_t>::max)()};
    std::size_t maximum_workspace_bytes{(std::numeric_limits<std::size_t>::max)()};
    std::size_t maximum_staging_bytes{(std::numeric_limits<std::size_t>::max)()};
    std::size_t reserved_engine_bytes{};
    std::size_t reserved_staging_bytes{};
    parallel_engine_workspace_slot* parallel_observer{};
    std::size_t engine_bytes{};
    std::size_t staging_bytes{};
    std::size_t engine_high_water{};
    std::size_t staging_high_water{};
    std::size_t work{};

    [[nodiscard]] auto check_workspace_bytes(
        workspace_category category, std::size_t next,
        std::size_t live_credit, std::size_t reservation_credit) const noexcept -> clipper_error_code {
        const auto live = category == workspace_category::engine ? engine_bytes : staging_bytes;
        const auto reserved = category == workspace_category::engine ? reserved_engine_bytes : reserved_staging_bytes;
        const auto limit = category == workspace_category::engine ? maximum_workspace_bytes : maximum_staging_bytes;
        return next > limit - (reserved - reservation_credit) - (live - live_credit)
            ? clipper_error_code::resource_limit : clipper_error_code::ok;
    }

    // Owners publish only completed effective storage. Admission or a growth
    // reservation already protects an increase; it is never admitted twice.
    CLIPPER2NEXT_RESOURCE_FORCE_INLINE auto publish_live_bytes(
        workspace_category category, std::size_t previous, std::size_t next) noexcept -> void {
        auto& total = category == workspace_category::engine ? engine_bytes : staging_bytes;
        total = total - previous + next;
        auto& peak = category == workspace_category::engine ? engine_high_water : staging_high_water;
        peak = (std::max)(peak, total);
        if (parallel_observer && category == workspace_category::engine) {
            parallel_observer->change(previous, next);
        }
    }
    // Returning effective storage cannot consume quota or raise a peak.
    CLIPPER2NEXT_RESOURCE_FORCE_INLINE auto release_live_bytes(
        workspace_category category, std::size_t previous, std::size_t next) noexcept -> void {
        assert(next <= previous);
        auto& total = category == workspace_category::engine ? engine_bytes : staging_bytes;
        total -= previous - next;
        if (parallel_observer && category == workspace_category::engine) {
            parallel_observer->change(previous, next);
        }
    }

    [[nodiscard]] auto try_add_work(std::size_t amount) noexcept -> clipper_error_code {
        if (amount > maximum_work - work) { return clipper_error_code::resource_limit; }
        work += amount;
        return clipper_error_code::ok;
    }
};

// A token belongs to one logical buffer, not to its allocator capacity. No
// registry, allocation, TLS lookup, or allocator-relocation charge is needed.
class scoped_workspace_charge final {
public:
    scoped_workspace_charge() noexcept = default;
    explicit scoped_workspace_charge(execution_resource_ledger* context,
                                     workspace_category category = workspace_category::engine) noexcept
        : context_(context), category_(category) {}
    scoped_workspace_charge(const scoped_workspace_charge&) = delete;
    auto operator=(const scoped_workspace_charge&) -> scoped_workspace_charge& = delete;
    scoped_workspace_charge(scoped_workspace_charge&& other) noexcept
        : context_(std::exchange(other.context_, nullptr)), category_(other.category_),
          bytes_(std::exchange(other.bytes_, 0U)) {}
    auto operator=(scoped_workspace_charge&& other) noexcept -> scoped_workspace_charge& {
        if (this != &other) {
            release();
            context_ = std::exchange(other.context_, nullptr);
            category_ = other.category_;
            bytes_ = std::exchange(other.bytes_, 0U);
        }
        return *this;
    }
    ~scoped_workspace_charge() { release(); }

    [[nodiscard]] auto context() const noexcept -> execution_resource_ledger* { return context_; }
    [[nodiscard]] auto category() const noexcept -> workspace_category { return category_; }
    [[nodiscard]] auto bytes() const noexcept -> std::size_t { return bytes_; }
    [[nodiscard]] auto try_set_slots(std::size_t slots, std::size_t element_bytes) noexcept
        -> clipper_error_code {
        const auto maximum = (std::numeric_limits<std::size_t>::max)();
        if (slots != 0U && element_bytes > maximum / slots) { return clipper_error_code::resource_limit; }
        const auto next = slots * element_bytes;
        if (context_) {
            const auto error = context_->check_workspace_bytes(category_, next, bytes_, 0U);
            if (error != clipper_error_code::ok) { return error; }
        }
        commit_live_bytes(next);
        return clipper_error_code::ok;
    }
    auto set_live_slots(std::size_t slots, std::size_t element_bytes) -> void {
        if (const auto error = try_set_slots(slots, element_bytes); error != clipper_error_code::ok) { raise_clipper_error(error); }
    }
    // Construction must not allocate, query the ledger, reenter, or change
    // another owner. Publish the real completed object before returning.
    template<class Construct>
    [[nodiscard]] CLIPPER2NEXT_RESOURCE_FORCE_INLINE auto try_construct_bytes(
        std::size_t next, Construct&& construct) noexcept -> clipper_error_code {
        return try_construct_bytes(next, context_, category_, std::forward<Construct>(construct));
    }
    template<workspace_category Category, class Construct>
    [[nodiscard]] CLIPPER2NEXT_RESOURCE_FORCE_INLINE auto try_construct_bytes(
        std::size_t next, Construct&& construct) noexcept -> clipper_error_code {
        assert(category_ == Category);
        return try_construct_bytes(next, context_, Category, std::forward<Construct>(construct));
    }
    [[nodiscard]] auto try_reclassify(workspace_category category) noexcept -> clipper_error_code {
        if (category == category_) { return clipper_error_code::ok; }
        if (context_) {
            auto& target = category == workspace_category::engine ? context_->engine_bytes : context_->staging_bytes;
            const auto limit = category == workspace_category::engine ? context_->maximum_workspace_bytes : context_->maximum_staging_bytes;
            const auto reserved = category == workspace_category::engine ? context_->reserved_engine_bytes : context_->reserved_staging_bytes;
            if (bytes_ > limit - reserved - target) { return clipper_error_code::resource_limit; }
            auto& source = category_ == workspace_category::engine ? context_->engine_bytes : context_->staging_bytes;
            // Generation workers have maximum_staging_bytes=0 and never
            // reclassify a nonempty owner. Publication runs after quiescence.
            assert(context_->parallel_observer == nullptr || bytes_ == 0U);
            source -= bytes_;
            target += bytes_;
            auto& peak = category == workspace_category::engine ? context_->engine_high_water : context_->staging_high_water;
            peak = (std::max)(peak, target);
        }
        category_ = category;
        return clipper_error_code::ok;
    }
    // Owners of one concrete storage phase share their root and category.
    // Clearing that phase and publishing its decrease form one transition.
    template<std::size_t Count, class Clear>
    static auto release_storage(
        const std::array<scoped_workspace_charge*, Count>& owners, Clear&& clear) noexcept -> void {
        static_assert(Count != 0);
        static_assert(std::is_nothrow_invocable_v<Clear&>);
        auto* context = owners.front()->context_;
        const auto category = owners.front()->category_;
        for (const auto* owner : owners) {
            assert(owner->context_ == context && owner->category_ == category);
        }
        if (!context) {
            clear();
            for (auto* owner : owners) { owner->bytes_ = 0U; }
            return;
        }
        auto previous = std::size_t{};
        for (const auto* owner : owners) {
            assert(owner->bytes_ <= (std::numeric_limits<std::size_t>::max)() - previous);
            previous += owner->bytes_;
        }
        auto finish = [&]() noexcept {
            clear();
            for (auto* owner : owners) { owner->bytes_ = 0U; }
            auto& total = category == workspace_category::engine
                ? context->engine_bytes : context->staging_bytes;
            assert(total >= previous);
            total -= previous;
        };
        if (context->parallel_observer && category == workspace_category::engine && previous != 0U) {
            context->parallel_observer->reclaim(previous, &finish,
                +[](void* action) noexcept { (*static_cast<decltype(finish)*>(action))(); });
        } else {
            finish();
        }
    }

    auto release() noexcept -> void {
        release_bytes(bytes_);
    }
    // Returning owned storage cannot consume quota or raise a high-water mark.
    auto release_bytes(std::size_t amount) noexcept -> void {
        assert(amount <= bytes_);
        const auto previous = bytes_;
        bytes_ -= amount;
        if (context_) { context_->release_live_bytes(category_, previous, bytes_); }
    }
private:
    friend class scoped_workspace_growth;
    template<class Construct>
    [[nodiscard]] CLIPPER2NEXT_RESOURCE_FORCE_INLINE auto try_construct_bytes(std::size_t next, execution_resource_ledger* context,
                                           workspace_category category, Construct&& construct) noexcept -> clipper_error_code {
        static_assert(std::is_nothrow_invocable_v<Construct&>);
        assert(next >= bytes_);
        const auto added = next - bytes_;
        if (context) {
            const auto error = context->check_workspace_bytes(category, added, 0U, 0U);
            if (error != clipper_error_code::ok) { return error; }
        }
        construct();
        // The no-callout constructor cannot change its owner. Reuse the
        // admitted positive delta, not an alias-sensitive post-construction
        // owner read. Root totals are still read and published only now.
        bytes_ = next;
        if (context) { context->publish_live_bytes(category, 0U, added); }
        return clipper_error_code::ok;
    }
    // Explicit admission or a growth reservation already protects these bytes.
    // Publishing constructed storage must not admit the same growth again.
    auto commit_live_bytes(std::size_t next) noexcept -> void {
        commit_live_bytes(next, context_, category_);
    }
    auto commit_live_bytes(std::size_t next, execution_resource_ledger* context,
                           workspace_category category) noexcept -> void {
        const auto previous = bytes_;
        bytes_ = next;
        if (context) { context->publish_live_bytes(category, previous, next); }
    }
    execution_resource_ledger* context_{};
    workspace_category category_{};
    std::size_t bytes_{};
};

// Admission quota is not an effective owner and never raises live high-water.
class scoped_workspace_reservation final {
public:
    explicit scoped_workspace_reservation(execution_resource_ledger* root,
                                         workspace_category category = workspace_category::engine) noexcept
        : root_(root), category_(category) {}
    scoped_workspace_reservation(const scoped_workspace_reservation&) = delete;
    auto operator=(const scoped_workspace_reservation&) -> scoped_workspace_reservation& = delete;
    ~scoped_workspace_reservation() { release(); }
    [[nodiscard]] auto try_set_bytes(std::size_t next) noexcept -> clipper_error_code {
        if (root_) {
            const auto error = root_->check_workspace_bytes(category_, next, 0U, bytes_);
            if (error != clipper_error_code::ok) { return error; }
            auto& reserved = category_ == workspace_category::engine ? root_->reserved_engine_bytes : root_->reserved_staging_bytes;
            reserved = reserved - bytes_ + next;
        }
        bytes_ = next;
        return clipper_error_code::ok;
    }
    auto set_bytes(std::size_t next) -> void {
        if (const auto error = try_set_bytes(next); error != clipper_error_code::ok) { raise_clipper_error(error); }
    }
    [[nodiscard]] auto bytes() const noexcept -> std::size_t { return bytes_; }
    auto release() noexcept -> void {
        if (root_) {
            auto& reserved = category_ == workspace_category::engine ? root_->reserved_engine_bytes : root_->reserved_staging_bytes;
            reserved -= bytes_;
        }
        bytes_ = 0U;
    }
private:
    friend class scoped_workspace_growth;
    auto commit_reserved_bytes(std::size_t next, execution_resource_ledger* root,
                               workspace_category category) noexcept -> void {
        if (root) {
            auto& reserved = category == workspace_category::engine ? root->reserved_engine_bytes : root->reserved_staging_bytes;
            reserved = reserved - bytes_ + next;
        }
        bytes_ = next;
    }
    execution_resource_ledger* root_{};
    workspace_category category_{};
    std::size_t bytes_{};
};

// Fallible growth first protects headroom. Only successful effective storage
// changes commit to live/peak. A batch can commit a successful prefix while
// retaining quota for its remaining writes; failure releases only that quota.
class scoped_workspace_growth final {
public:
    explicit scoped_workspace_growth(scoped_workspace_charge* owner) noexcept
        : owner_(owner), reservation_(owner ? owner->context() : nullptr,
                                      owner ? owner->category() : workspace_category::engine) {}
    scoped_workspace_growth(scoped_workspace_charge* owner, std::size_t slots, std::size_t element_bytes)
        : scoped_workspace_growth(owner) {
        if (const auto error = try_reserve_slots(slots, element_bytes); error != clipper_error_code::ok) { raise_clipper_error(error); }
    }
    scoped_workspace_growth(scoped_workspace_charge& owner, std::size_t slots, std::size_t element_bytes)
        : scoped_workspace_growth(&owner, slots, element_bytes) {}
    scoped_workspace_growth(const scoped_workspace_growth&) = delete;
    auto operator=(const scoped_workspace_growth&) -> scoped_workspace_growth& = delete;
    [[nodiscard]] auto try_reserve_slots(std::size_t slots, std::size_t element_bytes) noexcept -> clipper_error_code {
        const auto maximum = (std::numeric_limits<std::size_t>::max)();
        if (slots && element_bytes > maximum / slots) { return clipper_error_code::resource_limit; }
        const auto target = slots * element_bytes;
        const auto current = owner_ ? owner_->bytes() : 0U;
        const auto error = reservation_.try_set_bytes(target > current ? target - current : 0U);
        if (error == clipper_error_code::ok) { target_ = target; }
        return error;
    }
    auto commit_slots(std::size_t slots, std::size_t element_bytes) noexcept -> void {
        assert(slots == 0U || element_bytes <= (std::numeric_limits<std::size_t>::max)() / slots);
        const auto actual = slots * element_bytes;
        assert(actual <= target_);
        assert(!owner_ || (owner_->context_ == reservation_.root_ &&
                          owner_->category_ == reservation_.category_));
        // One owning transition replaces live storage and its admitted quota
        // with a successful prefix and its remainder. A shrinking prefix must
        // release old live bytes before the remainder is protected.
        const auto root = reservation_.root_;
        const auto category = reservation_.category_;
        if (owner_) { owner_->commit_live_bytes(actual, root, category); }
        reservation_.commit_reserved_bytes(target_ - actual, root, category);
    }
    auto commit() noexcept -> void { commit_slots(target_, 1U); }
private:
    scoped_workspace_charge* owner_{};
    scoped_workspace_reservation reservation_;
    std::size_t target_{};
};

} // namespace clipper2next::internal

#undef CLIPPER2NEXT_RESOURCE_FORCE_INLINE
