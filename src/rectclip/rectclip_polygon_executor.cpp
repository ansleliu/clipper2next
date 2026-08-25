#include "rectclip/private/rectclip_polygon_executor.h"

#include "clipper2next/geometry/core.h"
#include "rectclip/private/rectclip_classifier.h"
#include "rectclip/private/rectclip_edges.h"
#include "rectclip/private/rectclip_execution_context.h"
#include "rectclip/private/rectclip_intersections.h"
#include "rectclip/private/rectclip_traversal.h"

#include <cmath>
#include <cstddef>
#include <span>

namespace {
using Location = clipper2next::internal::rect_location;

[[nodiscard]] auto get_location(const clipper2next::Rect64& rect,
                                const clipper2next::Point64& point,
                                Location& location) -> bool {
    location = clipper2next::internal::classify_point(rect, point);
    return !clipper2next::internal::is_on_rect_boundary(rect, point);
}

[[nodiscard]] auto path_contains_path(const clipper2next::Path64& path1,
                                      std::span<const clipper2next::Point64> path2) -> bool {
    int io_count = 0;
    for (const auto& point : path2) {
        const auto pip = clipper2next::point_in_polygon(point, path1);
        switch (pip) {
        case clipper2next::PointInPolygonResult::IsOutside: {
            ++io_count;
            break;
        }
        case clipper2next::PointInPolygonResult::IsInside: {
            --io_count;
            break;
        }
        default: {
            continue;
        }
        }
        if (std::abs(io_count) > 1) { break; }
    }
    return io_count <= 0;
}

auto finish_path(clipper2next::internal::rectclip_context& storage,
                 clipper2next::internal::rectclip_execution_context& execution,
                 const clipper2next::Path64& path,
                 Location starting_location,
                 Location first_cross,
                 Location location) -> void {
    if (first_cross == Location::Inside) {
        if (starting_location != Location::Inside && storage.path_bounds.contains(storage.rect) &&
            path_contains_path(path, storage.rect_as_path)) {
            const bool clockwise =
                clipper2next::internal::start_locations_are_clockwise(storage.start_locs);
            for (std::size_t index = 0; index < 4U; ++index) {
                const std::size_t corner = clockwise ? index : 3U - index;
                execution.add(storage.rect_as_path[corner]);
                clipper2next::internal::add_to_edge(
                    storage.edges[corner * 2U], storage.results[0]);
            }
        }
        return;
    }
    if (location == Location::Inside ||
        (location == first_cross && storage.start_locs.size() <= 2U)) {
        return;
    }
    if (!storage.start_locs.empty()) {
        auto previous = location;
        for (auto next : storage.start_locs) {
            if (previous == next) { continue; }
            execution.add_corner(
                previous, clipper2next::internal::heading_clockwise(previous, next));
            previous = next;
        }
        location = previous;
    }
    if (location != first_cross) {
        execution.add_corner(
            location, clipper2next::internal::heading_clockwise(location, first_cross));
    }
}
}  // namespace

namespace clipper2next::internal {
rectclip_polygon_executor::rectclip_polygon_executor(rectclip_context& storage) noexcept
    : storage_(&storage) {}

auto rectclip_polygon_executor::storage() noexcept -> rectclip_context& {
    return *storage_;
}

auto rectclip_polygon_executor::storage() const noexcept -> const rectclip_context& {
    return *storage_;
}

auto rectclip_polygon_executor::execute_path(rectclip_execution_context& execution,
                                             const Path64& path) -> void {
    if (path.empty()) { return; }

    const size_t high_i = path.size() - 1;
    Location prev = Location::Inside;
    Location loc;
    Location crossing_loc = Location::Inside;
    Location first_cross = Location::Inside;
    if (!get_location(storage_->rect, path[high_i], loc)) {
        size_t i = high_i;
        while (i > 0 && !get_location(storage_->rect, path[i - 1], prev)) { --i; }
        if (i == 0) {
            for (const auto& point : path) { execution.add(point); }
            return;
        }
        if (prev == Location::Inside) { loc = Location::Inside; }
    }
    const Location starting_loc = loc;

    size_t i = 0;
    while (i <= high_i) {
        prev = loc;
        const Location crossing_prev = crossing_loc;

        execution.get_next_location(path, loc, i, high_i);

        if (i > high_i) { break; }
        Point64 ip;
        Point64 ip2;
        const Point64 prev_pt = i ? path[static_cast<size_t>(i - 1)] : path[high_i];

        crossing_loc = loc;
        if (!get_intersection(storage_->rect, path[i], prev_pt, crossing_loc, ip)) {
            if (crossing_prev == Location::Inside) {
                const bool clockwise =
                    is_clockwise(prev, loc, prev_pt, path[i], storage_->rect_midpoint);
                do {
                    storage_->start_locs.emplace_back(prev);
                    prev = adjacent_location(prev, clockwise);
                } while (prev != loc);
                crossing_loc = crossing_prev;
            } else if (prev != Location::Inside && prev != loc) {
                const bool clockwise =
                    is_clockwise(prev, loc, prev_pt, path[i], storage_->rect_midpoint);
                do { execution.add_corner(prev, clockwise); } while (prev != loc);
            }
            ++i;
            continue;
        }

        if (loc == Location::Inside) {
            if (first_cross == Location::Inside) {
                first_cross = crossing_loc;
                storage_->start_locs.emplace_back(prev);
            } else if (prev != crossing_loc) {
                const bool clockwise =
                    is_clockwise(prev, crossing_loc, prev_pt, path[i], storage_->rect_midpoint);
                do { execution.add_corner(prev, clockwise); } while (prev != crossing_loc);
            }
        } else if (prev != Location::Inside) {
            loc = prev;
            if (!get_intersection(storage_->rect, prev_pt, path[i], loc, ip2)) {
                ++i;
                continue;
            }
            if (crossing_prev != Location::Inside && crossing_prev != loc) {
                execution.add_corner(crossing_prev, loc);
            }

            if (first_cross == Location::Inside) {
                first_cross = loc;
                storage_->start_locs.emplace_back(prev);
            }

            loc = crossing_loc;
            execution.add(ip2);
            if (ip == ip2) {
                static_cast<void>(get_location(storage_->rect, path[i], loc));
                execution.add_corner(crossing_loc, loc);
                crossing_loc = loc;
                continue;
            }
        } else {
            loc = crossing_loc;
            if (first_cross == Location::Inside) { first_cross = crossing_loc; }
        }

        execution.add(ip);
    }

    finish_path(*storage_, execution, path, starting_loc, first_cross, loc);
}
}  // namespace clipper2next::internal
