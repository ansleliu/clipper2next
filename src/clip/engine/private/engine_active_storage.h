#pragma once
#include "clip/engine/private/engine_types.h"
#include "support/private/execution_resource_ledger.h"
#include "support/private/storage/stable_block_storage.h"
#include <memory>
#include <utility>

namespace clipper2next::internal {

// Active edges end at AEL removal. Their stable slots then belong to cache,
// rather than remaining effective engine data until the whole request ends.
class engine_active_storage final {
    static_assert(std::is_trivially_destructible_v<active_edge_node>);
public:
    engine_active_storage() = default;
    engine_active_storage(const engine_active_storage&) = delete;
    auto operator=(const engine_active_storage&) -> engine_active_storage& = delete;
    ~engine_active_storage() { clear(); }

    auto bind(execution_resource_ledger* context) -> void {
        detach();
        const auto bytes = checked_size_multiply(active_count_, sizeof(active_edge_node));
        if (context) {
            const auto error = context->check_workspace_bytes(workspace_category::engine, bytes, 0U, 0U);
            if (error != clipper_error_code::ok) { raise_clipper_error(error); }
            context_ = context;
            context->publish_live_bytes(workspace_category::engine, 0U, bytes);
        }
    }
    auto detach() noexcept -> void {
        if (auto* context = std::exchange(context_, nullptr)) {
            context->release_live_bytes(workspace_category::engine, live_bytes(), 0U);
        }
    }
    template<class... Args>
    auto emplace(Args&&... args) -> active_edge_node& {
        static_assert(std::is_nothrow_constructible_v<active_edge_node, Args...>);
        if (!free_ && storage_.size() == storage_.retained_capacity()) {
            return acquire_block(std::forward<Args>(args)...);
        }
        auto* context = context_;
        if (context) {
            const auto error = context->check_workspace_bytes(
                workspace_category::engine, sizeof(active_edge_node), 0U, 0U);
            if (error != clipper_error_code::ok) { raise_clipper_error(error); }
        }
        active_edge_node* result;
        if (free_) {
            auto* slot = free_;
            free_ = slot->jump.get();
            result = std::construct_at(slot, std::forward<Args>(args)...);
        } else {
            result = &storage_.emplace_retained(std::forward<Args>(args)...);
        }
        ++active_count_;
        if (context) { context->publish_live_bytes(workspace_category::engine, 0U, sizeof(active_edge_node)); }
        return *result;
    }
    auto retire(active_edge_node& edge) noexcept -> void {
        assert(active_count_ != 0U && !edge.prev_in_ael && !edge.next_in_ael);
        // Keep the cached trivial object alive: old top-edge snapshots may
        // still inspect its cleared AEL links during this scanbeam. No new
        // active edge is acquired until the next minima-insertion phase.
        // Its obsolete SEL run link now owns cache membership, not geometry.
        edge.jump = free_;
        free_ = &edge;
        --active_count_;
        if (context_) { context_->release_live_bytes(workspace_category::engine, sizeof(active_edge_node), 0U); }
    }
    auto clear() noexcept -> void {
        const auto previous = context_ ? live_bytes() : 0U;
        storage_.clear();
        free_ = nullptr;
        active_count_ = 0U;
        if (context_) { context_->release_live_bytes(workspace_category::engine, previous, 0U); }
    }
    auto release() noexcept -> void { clear(); storage_.release(); }
    auto empty() const noexcept -> bool { return active_count_ == 0U; }
    auto size() const noexcept -> std::size_t { return active_count_; }
    auto retained_capacity() const noexcept -> std::size_t { return storage_.retained_capacity(); }
private:
    auto live_bytes() const noexcept -> std::size_t {
        assert(active_count_ <= (std::numeric_limits<std::size_t>::max)() / sizeof(active_edge_node));
        return active_count_ * sizeof(active_edge_node);
    }
    template<class... Args>
#if defined(_MSC_VER)
    __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
    __attribute__((noinline))
#endif
    auto acquire_block(Args&&... args) -> active_edge_node& {
        auto* context = context_;
        auto quota = scoped_workspace_reservation{context};
        quota.set_bytes(sizeof(active_edge_node));
        auto& result = storage_.emplace(std::forward<Args>(args)...);
        ++active_count_;
        assert(context_ == context);
        quota.release();
        if (context) { context->publish_live_bytes(workspace_category::engine, 0U, sizeof(active_edge_node)); }
        return result;
    }
    execution_resource_ledger* context_{};
    stable_block_storage<active_edge_node> storage_;
    active_edge_node* free_{};
    std::size_t active_count_{};
};

} // namespace clipper2next::internal
