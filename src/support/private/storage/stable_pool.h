#pragma once

#include <array>
#include <cstddef>
#include <limits>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

namespace clipper2next::internal {

template <class T, std::size_t BlockSize = 1024U>
class stable_pool final {
public:
    stable_pool() = default;
    stable_pool(const stable_pool&) = delete;
    auto operator=(const stable_pool&) -> stable_pool& = delete;
    ~stable_pool() { clear(); }

    template <class... Args>
    [[nodiscard]] auto emplace(Args&&... args) -> T& {
        if (size_ == retained_capacity()) {
            blocks_.push_back(std::make_unique_for_overwrite<block_type>());
        }
        auto* result = ptr_at(size_);
        std::construct_at(result, std::forward<Args>(args)...);
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
        decltype(blocks_){}.swap(blocks_);
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

    [[nodiscard]] auto ptr_at(std::size_t index) noexcept -> T* {
        auto& slot = (*blocks_[index / BlockSize])[index % BlockSize];
        return std::launder(reinterpret_cast<T*>(slot.storage));
    }

    [[nodiscard]] auto ptr_at(std::size_t index) const noexcept -> const T* {
        const auto& slot = (*blocks_[index / BlockSize])[index % BlockSize];
        return std::launder(reinterpret_cast<const T*>(slot.storage));
    }

    std::vector<std::unique_ptr<block_type>> blocks_;
    std::size_t size_{0};
};

}  // namespace clipper2next::internal
