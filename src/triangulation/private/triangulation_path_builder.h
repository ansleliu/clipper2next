#pragma once

#include "clipper2next/core.h"

#include <cstddef>
#include <span>
#include <vector>

namespace clipper2next::internal {

// Owns cleaned source coordinates during boundary preparation. Views remain
// valid until append/clear/release; descriptors use the original size_t domain.
class triangulation_source_paths final {
public:
    [[nodiscard]] auto append(const Paths64& input) -> bool;
    [[nodiscard]] auto empty() const noexcept -> bool { return ends_.empty(); }
    [[nodiscard]] auto size() const noexcept -> std::size_t { return ends_.size(); }
    [[nodiscard]] auto point_count() const noexcept -> std::size_t { return points_.size(); }
    [[nodiscard]] auto retains_storage() const noexcept -> bool {
        return points_.capacity() != 0U || ends_.capacity() != 0U;
    }
    [[nodiscard]] auto operator[](std::size_t i) const noexcept -> std::span<const Point64> {
        const auto first = i ? ends_[i - 1U] : 0U;
        return std::span<const Point64>{points_}.subspan(first, ends_[i] - first);
    }
    [[nodiscard]] auto mutable_path(std::size_t i) noexcept -> std::span<Point64> {
        const auto first = i ? ends_[i - 1U] : 0U;
        return std::span<Point64>{points_}.subspan(first, ends_[i] - first);
    }
    auto clear() noexcept -> void { points_.clear(); ends_.clear(); }
    auto release() noexcept -> void;

private:
    std::vector<Point64> points_;
    std::vector<std::size_t> ends_;
};

}  // namespace clipper2next::internal
