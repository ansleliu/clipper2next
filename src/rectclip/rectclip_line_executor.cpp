#include "rectclip/private/rectclip_line_executor.h"

#include "rectclip/private/rectclip_classifier.h"
#include "rectclip/private/rectclip_execution_context.h"
#include "rectclip/private/rectclip_intersections.h"

namespace {
using Location = clipper2next::internal::rect_location;

[[nodiscard]] auto get_location(const clipper2next::Rect64& rect,
                                const clipper2next::Point64& point,
                                Location& location) -> bool {
    location = clipper2next::internal::classify_point(rect, point);
    return !clipper2next::internal::is_on_rect_boundary(rect, point);
}
}  // namespace

namespace clipper2next::internal {
rectclip_line_executor::rectclip_line_executor(rectclip_context& storage) noexcept
    : storage_(&storage) {}

auto rectclip_line_executor::storage() noexcept -> rectclip_context& {
    return *storage_;
}

auto rectclip_line_executor::storage() const noexcept -> const rectclip_context& {
    return *storage_;
}

auto rectclip_line_executor::execute_path(rectclip_execution_context& execution, const Path64& path)
    -> void {
    if (storage_->rect.is_empty() || path.size() < 2) { return; }

    storage_->results.clear();
    storage_->op_container.clear();
    storage_->start_locs.clear();

    size_t i = 1;
    const size_t high_i = path.size() - 1;

    Location prev = Location::Inside;
    Location loc;
    Location crossing_loc;
    if (!get_location(storage_->rect, path[0], loc)) {
        while (i <= high_i && !get_location(storage_->rect, path[i], prev)) { ++i; }
        if (i > high_i) {
            for (const auto& point : path) { execution.add(point); }
            return;
        }
        if (prev == Location::Inside) { loc = Location::Inside; }
        i = 1;
    }
    if (loc == Location::Inside) { execution.add(path[0]); }

    while (i <= high_i) {
        prev = loc;
        execution.get_next_location(path, loc, i, high_i);
        if (i > high_i) { break; }
        Point64 ip;
        Point64 ip2;
        const Point64 prev_pt = path[static_cast<size_t>(i - 1)];

        crossing_loc = loc;
        if (!get_intersection(storage_->rect, path[i], prev_pt, crossing_loc, ip)) {
            ++i;
            continue;
        }

        if (loc == Location::Inside) {
            execution.add(ip, true);
        } else if (prev != Location::Inside) {
            crossing_loc = prev;
            if (!get_intersection(storage_->rect, prev_pt, path[i], crossing_loc, ip2)) {
                ++i;
                continue;
            }
            execution.add(ip2, true);
            execution.add(ip);
        } else {
            execution.add(ip);
        }
    }
}
}  // namespace clipper2next::internal
