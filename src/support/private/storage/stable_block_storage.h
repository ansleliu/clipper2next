#pragma once
#include "support/private/checked_size.h"
#include "support/private/storage/release_vector.h"

#include <array>
#include <cassert>
#include <cstddef>
#include <limits>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

namespace clipper2next::internal {

// Owns live objects and retained blocks with stable object addresses.

template <class T, std::size_t BlockSize = 1024U>
class stable_block_storage final {
public:
    stable_block_storage() = default;
    stable_block_storage(const stable_block_storage&) = delete;
    auto operator=(const stable_block_storage&) -> stable_block_storage& = delete;
    ~stable_block_storage() { clear(); }


    template <class... Args>
    [[nodiscard]] auto emplace(Args&&... args) -> T& {
        static_cast<void>(checked_size_add(size_, 1U));
        if (size_ / BlockSize == blocks_.size()) {
            allocate_block();
        }
        return emplace_retained(std::forward<Args>(args)...);
    }

    // The caller has acquired a physical slot and checked count growth.
    template <class... Args>
    [[nodiscard]] auto emplace_retained(Args&&... args)
        noexcept(std::is_nothrow_constructible_v<T, Args...>) -> T& {
        assert(size_ < (std::numeric_limits<std::size_t>::max)() &&
               size_ / BlockSize < blocks_.size());
        // Construct in raw storage; launder is only for accessing live slots.
        auto& storage = (*blocks_[size_ / BlockSize])[size_ % BlockSize].storage;
        auto* result = std::construct_at(
            reinterpret_cast<T*>(storage), std::forward<Args>(args)...);
        ++size_;
        return *result;
    }

    auto clear() noexcept -> void {
        if constexpr (!std::is_trivially_destructible_v<T>) {
            for (std::size_t index = 0; index < size_; ++index) { std::destroy_at(ptr_at(index)); }
        }
        size_ = 0;
    }

    // Unlike clear(), also returns the block storage to the allocator.
    auto release() noexcept -> void {
        clear();
        release_vector(blocks_);
    }

    [[nodiscard]] auto empty() const noexcept -> bool { return size_ == 0; }

    [[nodiscard]] auto size() const noexcept -> std::size_t { return size_; }

    [[nodiscard]] auto retained_capacity() const noexcept -> std::size_t {
        const auto maximum = (std::numeric_limits<std::size_t>::max)();
        return blocks_.size() > maximum / BlockSize ? maximum : blocks_.size() * BlockSize;
    }

private:
    struct slot final {
        alignas(T) std::byte storage[sizeof(T)];
    };

    using block_type = std::array<slot, BlockSize>;

    // Block acquisition may allocate and call out; slot construction does not.
#if defined(_MSC_VER)
    __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
    __attribute__((noinline))
#endif
    auto allocate_block() -> void {
        blocks_.push_back(std::make_unique_for_overwrite<block_type>());
    }

    [[nodiscard]] auto ptr_at(std::size_t index) noexcept -> T* {
        auto& slot = (*blocks_[index / BlockSize])[index % BlockSize];
        return std::launder(reinterpret_cast<T*>(slot.storage));
    }


    std::vector<std::unique_ptr<block_type>> blocks_;
    std::size_t size_{0};
};

}  // namespace clipper2next::internal
