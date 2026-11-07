#pragma once
#include "support/private/execution_resource_ledger.h"
#include "support/private/checked_size.h"
#include "support/private/storage/stable_block_storage.h"

#include <cstddef>
#include <utility>

namespace clipper2next::internal {

template <class T, std::size_t BlockSize = 1024U>
class stable_pool final {
public:
    stable_pool() = default;
    stable_pool(const stable_pool&) = delete;
    auto operator=(const stable_pool&) -> stable_pool& = delete;
    ~stable_pool() { clear(); }

    auto bind(execution_resource_ledger* context) -> void {
        charge_ = scoped_workspace_charge{context};
        if (const auto error = charge_.try_set_slots(storage_.size(), sizeof(T)); error != clipper_error_code::ok) {
            detach();
            raise_clipper_error(error);
        }
    }
    auto detach() noexcept -> void { charge_ = scoped_workspace_charge{}; }

    template <class... Args>
    [[nodiscard]] auto emplace(Args&&... args) -> T& {
        if (!charge_.context()) { return storage_.emplace(std::forward<Args>(args)...); }
        return emplace_reserved(std::forward<Args>(args)...);
    }

    // The value constructor is no-throw and makes no external callouts.
    // New blocks retain ordinary reserved growth; retained slots need only
    // checked admission followed immediately by real construction/publication.
    template <class... Args>
    [[nodiscard]]
#if defined(_MSC_VER)
    __forceinline
#elif defined(__GNUC__) || defined(__clang__)
    inline __attribute__((always_inline))
#else
    inline
#endif
    auto emplace_direct(Args&&... args) -> T& {
        static_assert(std::is_nothrow_constructible_v<T, Args...>);
        if (!charge_.context()) { return storage_.emplace(std::forward<Args>(args)...); }
        if (storage_.size() == storage_.retained_capacity()) {
            return emplace_reserved(std::forward<Args>(args)...);
        }
        T* result{};
        const auto error = charge_.try_construct_bytes<workspace_category::engine>(
            checked_size_add(charge_.bytes(), sizeof(T)), [&]() noexcept {
                result = &storage_.emplace_retained(std::forward<Args>(args)...);
            });
        if (error != clipper_error_code::ok) { raise_clipper_error(error); }
        return *result;
    }

    auto clear() noexcept -> void {
        storage_.clear();
        charge_.release();
    }
    auto release() noexcept -> void {
        // Release the logical owner before block deallocation can call out.
        clear();
        storage_.release();
    }
    [[nodiscard]] auto empty() const noexcept -> bool { return storage_.empty(); }
    [[nodiscard]] auto size() const noexcept -> std::size_t { return storage_.size(); }
    [[nodiscard]] auto retained_capacity() const noexcept -> std::size_t {
        return storage_.retained_capacity();
    }

private:
    template <class... Args>
#if defined(_MSC_VER)
    __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
    __attribute__((noinline))
#endif
    auto emplace_reserved(Args&&... args) -> T& {
        auto growth = scoped_workspace_growth{
            charge_, checked_size_add(charge_.bytes(), sizeof(T)), 1U};
        auto& result = storage_.emplace(std::forward<Args>(args)...);
        growth.commit();
        return result;
    }
    scoped_workspace_charge charge_;
    stable_block_storage<T, BlockSize> storage_;
};

}  // namespace clipper2next::internal
