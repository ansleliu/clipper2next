#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace clipper2next::internal {

template <class Tag>
struct topology_handle final {
    static constexpr std::size_t invalid_index = (std::numeric_limits<std::size_t>::max)();

    std::size_t index{invalid_index};
    std::uint32_t generation{0};

    [[nodiscard]] constexpr explicit operator bool() const noexcept {
        return index != invalid_index && generation != 0;
    }

    [[nodiscard]] friend constexpr auto operator==(topology_handle first,
                                                   topology_handle second) noexcept
        -> bool = default;
};

template <class T, class Tag>
class topology_ref final {
public:
    constexpr topology_ref() noexcept = default;
    constexpr topology_ref(std::nullptr_t) noexcept {}
    constexpr topology_ref(T& value) noexcept
        : ptr_(&value) {}
    constexpr topology_ref(T* value) noexcept
        : ptr_(value) {}
    constexpr topology_ref(const topology_ref&) noexcept = default;
    constexpr auto operator=(const topology_ref&) noexcept -> topology_ref& = default;

    constexpr auto operator=(std::nullptr_t) noexcept -> topology_ref& {
        ptr_ = nullptr;
        return *this;
    }

    constexpr auto operator=(T& value) noexcept -> topology_ref& {
        ptr_ = &value;
        return *this;
    }

    constexpr auto operator=(T* value) noexcept -> topology_ref& {
        ptr_ = value;
        return *this;
    }

    [[nodiscard]] constexpr auto get() const noexcept -> T* { return ptr_; }
    [[nodiscard]] explicit constexpr operator bool() const noexcept { return ptr_ != nullptr; }
    [[nodiscard]] constexpr operator T*() const noexcept { return ptr_; }
    [[nodiscard]] constexpr auto operator*() const noexcept -> T& { return *ptr_; }
    [[nodiscard]] constexpr auto operator->() const noexcept -> T* { return ptr_; }

    [[nodiscard]] friend constexpr auto operator==(topology_ref lhs, topology_ref rhs) noexcept
        -> bool {
        return lhs.ptr_ == rhs.ptr_;
    }

    [[nodiscard]] friend constexpr auto operator==(topology_ref lhs, std::nullptr_t) noexcept
        -> bool {
        return lhs.ptr_ == nullptr;
    }

    [[nodiscard]] friend constexpr auto operator==(std::nullptr_t, topology_ref rhs) noexcept
        -> bool {
        return rhs.ptr_ == nullptr;
    }

    [[nodiscard]] friend constexpr auto operator==(topology_ref lhs, T* rhs) noexcept -> bool {
        return lhs.ptr_ == rhs;
    }

    [[nodiscard]] friend constexpr auto operator==(T* lhs, topology_ref rhs) noexcept -> bool {
        return lhs == rhs.ptr_;
    }

private:
    T* ptr_ = nullptr;
};

template <class T, class Tag>
class topology_store final {
public:
    using value_type = T;
    using handle_type = topology_handle<Tag>;

    topology_store() = default;
    topology_store(const topology_store&) = delete;
    auto operator=(const topology_store&) -> topology_store& = delete;

    template <class... Args>
    [[nodiscard]] auto emplace(Args&&... args) -> handle_type {
        if (!free_list_.empty()) {
            const auto index = free_list_.back();
            free_list_.pop_back();
            auto& slot = slots_[index];
            slot.value.emplace(std::forward<Args>(args)...);
            return handle_type{index, slot.generation};
        }

        const auto index = slots_.size();
        slots_.push_back(slot{generation_epoch_, std::nullopt});
        slots_.back().value.emplace(std::forward<Args>(args)...);
        return handle_type{index, slots_.back().generation};
    }

    [[nodiscard]] auto contains(handle_type handle) const noexcept -> bool {
        return handle.index < slots_.size() &&
               slots_[handle.index].generation == handle.generation &&
               slots_[handle.index].value.has_value();
    }

    [[nodiscard]] auto get(handle_type handle) noexcept -> T* {
        if (!contains(handle)) { return nullptr; }
        return &*slots_[handle.index].value;
    }

    [[nodiscard]] auto get(handle_type handle) const noexcept -> const T* {
        if (!contains(handle)) { return nullptr; }
        return &*slots_[handle.index].value;
    }

    [[nodiscard]] auto ref(handle_type handle) -> T& {
        auto* value = get(handle);
        assert(value != nullptr);
        return *value;
    }

    [[nodiscard]] auto ref(handle_type handle) const -> const T& {
        const auto* value = get(handle);
        assert(value != nullptr);
        return *value;
    }

    auto erase(handle_type handle) -> bool {
        if (!contains(handle)) { return false; }
        auto& slot = slots_[handle.index];
        slot.value.reset();
        slot.generation = next_generation(slot.generation);
        free_list_.push_back(handle.index);
        return true;
    }

    auto clear() noexcept -> void {
        slots_.clear();
        free_list_.clear();
        generation_epoch_ = next_generation(generation_epoch_);
    }

    [[nodiscard]] auto size() const noexcept -> std::size_t {
        return slots_.size() - free_list_.size();
    }

    [[nodiscard]] auto empty() const noexcept -> bool { return size() == 0; }

    template <class Function>
    auto for_each(Function&& function) -> void {
        for (std::size_t index = 0; index < slots_.size(); ++index) {
            auto& slot = slots_[index];
            if (slot.value) { function(handle_type{index, slot.generation}, *slot.value); }
        }
    }

    template <class Function>
    auto for_each(Function&& function) const -> void {
        for (std::size_t index = 0; index < slots_.size(); ++index) {
            const auto& slot = slots_[index];
            if (slot.value) { function(handle_type{index, slot.generation}, *slot.value); }
        }
    }

private:
    struct slot final {
        std::uint32_t generation{1};
        std::optional<T> value{};
    };

    [[nodiscard]] static constexpr auto next_generation(std::uint32_t generation) noexcept
        -> std::uint32_t {
        const auto next = generation + 1;
        return next == 0 ? 1 : next;
    }

    std::vector<slot> slots_;
    std::vector<std::size_t> free_list_;
    std::uint32_t generation_epoch_{1};
};

}  // namespace clipper2next::internal
