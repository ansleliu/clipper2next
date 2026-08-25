#include "clip/engine/private/engine_path_builder.h"

#include <cstdlib>

namespace clipper2next::internal {

namespace {

auto points_really_close(const Point64& first, const Point64& second) noexcept -> bool {
    return (std::llabs(first.x - second.x) < 2) && (std::llabs(first.y - second.y) < 2);
}

template <typename Consumer>
[[nodiscard]] auto visit_path64(output_point_node* output_point,
                                bool reverse,
                                bool is_open,
                                Consumer&& consumer) -> std::size_t {
    if (!output_point || output_point->next == output_point ||
        (!is_open && output_point->next == output_point->prev)) {
        return 0U;
    }

    Point64 last_point;
    output_point_node* current;
    if (reverse) {
        last_point = output_point->pt;
        current = output_point->prev.get();
    } else {
        output_point = output_point->next.get();
        last_point = output_point->pt;
        current = output_point->next.get();
    }
    consumer(last_point);
    std::size_t count = 1U;

    while (current != output_point) {
        if (current->pt != last_point) {
            last_point = current->pt;
            consumer(last_point);
            ++count;
        }
        current = reverse ? current->prev.get() : current->next.get();
    }

    return is_open || count != 3U || !is_very_small_triangle(*current) ? count : 0U;
}

}  // namespace

auto is_very_small_triangle(const output_point_node& output_point) noexcept -> bool {
    return output_point.next->next == output_point.prev &&
           (points_really_close(output_point.prev->pt, output_point.next->pt) ||
            points_really_close(output_point.pt, output_point.next->pt) ||
            points_really_close(output_point.pt, output_point.prev->pt));
}

auto path64_point_count(output_point_node* output_point, bool reverse, bool is_open) noexcept
    -> std::size_t {
    return visit_path64(output_point, reverse, is_open, [](const Point64&) noexcept {});
}

auto build_path64(output_point_node* output_point, bool reverse, bool is_open, Path64& path)
    -> bool {
    path.clear();
    const auto count = visit_path64(
        output_point, reverse, is_open, [&path](const Point64& point) { path.emplace_back(point); });
    if (count == 0U) {
        path.clear();
        return false;
    }
    return true;
}

auto build_path64_into(output_point_node* output_point,
                       bool reverse,
                       bool is_open,
                       const std::span<geotypes::Point2i64> path) -> bool {
    auto* destination = path.data();
    const auto count = visit_path64(
        output_point, reverse, is_open, [&destination](const Point64& point) {
            *destination++ = {point.x, point.y};
        });
    return count != 0U && count == path.size();
}

auto build_pathd(output_point_node* output_point,
                 bool reverse,
                 bool is_open,
                 PathD& path,
                 double inverse_scale) -> bool {
    if (!output_point || output_point->next == output_point ||
        (!is_open && output_point->next == output_point->prev)) {
        return false;
    }

    path.resize(0);
    Point64 last_point;
    output_point_node* current;
    if (reverse) {
        last_point = output_point->pt;
        current = output_point->prev;
    } else {
        output_point = output_point->next;
        last_point = output_point->pt;
        current = output_point->next;
    }
    path.emplace_back(last_point.x * inverse_scale, last_point.y * inverse_scale);

    while (current != output_point) {
        if (current->pt != last_point) {
            last_point = current->pt;
            path.emplace_back(last_point.x * inverse_scale, last_point.y * inverse_scale);
        }
        current = reverse ? current->prev : current->next;
    }

    return is_open || path.size() != 3 || !is_very_small_triangle(*current);
}

}  // namespace clipper2next::internal
