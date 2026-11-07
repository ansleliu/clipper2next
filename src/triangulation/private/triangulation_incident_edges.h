#pragma once

#include "support/private/checked_size.h"
#include "support/private/storage/topology_store.h"

#include <algorithm>
#include <array>
#include <memory>
#include <memory_resource>
#include <type_traits>

namespace clipper2next::internal {

struct triangulation_edge;
struct triangulation_edge_tag;
using triangulation_edge_ref = topology_ref<triangulation_edge, triangulation_edge_tag>;

class triangulation_incident_edges final {
    static_assert(std::is_nothrow_copy_constructible_v<triangulation_edge_ref>);
    static_assert(std::is_trivially_destructible_v<triangulation_edge_ref>);

public:
    explicit triangulation_incident_edges(std::pmr::memory_resource* resource) noexcept
        : resource_(resource) {}
    triangulation_incident_edges(const triangulation_incident_edges&) = delete;
    auto operator=(const triangulation_incident_edges&) -> triangulation_incident_edges& = delete;
    ~triangulation_incident_edges() {
        if (data_ != local_.data()) {
            std::destroy_n(data_, size_);
            resource_->deallocate(data_, capacity_ * sizeof(triangulation_edge_ref),
                                  alignof(triangulation_edge_ref));
        }
    }

    auto push_back(triangulation_edge_ref edge) -> void {
        if (size_ == capacity_) { grow(); }
        std::construct_at(data_ + size_, edge);
        ++size_;
    }
    auto erase(triangulation_edge_ref* position) noexcept -> triangulation_edge_ref* {
        std::move(position + 1, end(), position);
        --size_;
        return position;
    }
    [[nodiscard]] auto size() const noexcept -> std::size_t { return size_; }
    [[nodiscard]] auto empty() const noexcept -> bool { return size_ == 0; }
    [[nodiscard]] auto begin() noexcept -> triangulation_edge_ref* { return data_; }
    [[nodiscard]] auto end() noexcept -> triangulation_edge_ref* { return data_ + size_; }
    [[nodiscard]] auto begin() const noexcept -> const triangulation_edge_ref* { return data_; }
    [[nodiscard]] auto end() const noexcept -> const triangulation_edge_ref* { return data_ + size_; }
    [[nodiscard]] auto operator[](std::size_t index) noexcept -> triangulation_edge_ref& {
        return data_[index];
    }
    [[nodiscard]] auto operator[](std::size_t index) const noexcept -> const triangulation_edge_ref& {
        return data_[index];
    }

private:
    // Keep allocator calls and relocation out of the retained append path,
    // as in stable_block_storage::allocate_block.
#if defined(_MSC_VER)
    __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
    __attribute__((noinline))
#endif
    auto grow() -> void {
        const auto capacity = checked_size_multiply(capacity_, 2U);
        const auto bytes = checked_size_multiply(capacity, sizeof(triangulation_edge_ref));
        auto* replacement = static_cast<triangulation_edge_ref*>(
            resource_->allocate(bytes, alignof(triangulation_edge_ref)));
        std::uninitialized_copy_n(data_, size_, replacement);
        auto* previous = data_;
        const auto previous_capacity = capacity_;
        data_ = replacement;
        capacity_ = capacity;
        if (previous != local_.data()) {
            std::destroy_n(previous, size_);
            resource_->deallocate(previous, previous_capacity * sizeof(triangulation_edge_ref),
                                  alignof(triangulation_edge_ref));
        }
    }

    std::pmr::memory_resource* resource_;
    std::array<triangulation_edge_ref, 4> local_{};
    triangulation_edge_ref* data_ = local_.data();
    std::size_t size_ = 0;
    std::size_t capacity_ = local_.size();
};

}  // namespace clipper2next::internal
