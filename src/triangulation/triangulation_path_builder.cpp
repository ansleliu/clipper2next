#include "triangulation/private/triangulation_path_builder.h"
#include "geometry/private/geometry_predicates.h"
#include "support/private/checked_size.h"
#include "support/private/storage/release_vector.h"

namespace clipper2next::internal {
namespace {
template<class Visit>
auto visit_source_points(const Path64& path, Visit&& visit) -> void {
    auto end = path.size();
    // Closing duplicates collapse to the same single omission as adjacent
    // duplicate removal followed by dropping the repeated first point.
    while (end > 1U && path[end - 1U] == path.front()) { --end; }
    for (std::size_t i = 0; i != end; ++i) {
        if (!i || path[i] != path[i - 1U]) { visit(path[i]); }
    }
}

auto source_point_count(const Path64& path) -> std::size_t {
    std::size_t count = 0;
    Point64 first{}, second{};
    bool noncollinear = false;
    visit_source_points(path, [&](const Point64& point) {
        if (!count) { first = point; }
        else if (count == 1U) { second = point; }
        else if (!noncollinear) {
            noncollinear = cross_product_sign_in_clipper_range(first, second, point) != 0;
        }
        ++count;
    });
    return count >= 3U && noncollinear ? count : 0U;
}
} // namespace

auto triangulation_source_paths::append(const Paths64& input) -> bool {
    std::size_t ring_count = 0;
    auto total = points_.size();
    for (const auto& path : input) {
        const auto count = source_point_count(path);
        if (!count) { continue; }
        total = checked_size_add(total, count);
        ++ring_count;
    }
    if (!ring_count) { return !empty(); }
    // Acquire both buffers before exposing any new source ring. Subsequent
    // Point64 copies and end-ordinal publication cannot allocate or call out.
    ends_.reserve(checked_size_add(ends_.size(), ring_count));
    points_.reserve(total);
    for (const auto& ring : input) {
        if (!source_point_count(ring)) { continue; }
        visit_source_points(ring, [&](const Point64& point) { points_.push_back(point); });
        ends_.push_back(points_.size());
    }
    return true;
}

auto triangulation_source_paths::release() noexcept -> void {
    clear();
    release_vector(points_);
    release_vector(ends_);
}
}
