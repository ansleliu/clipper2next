#include "rectclip/private/rectclip_intersections.h"

#include "geometry/private/geometry_predicates.h"

namespace clipper2next::internal {

auto is_horizontal(const Point64& first, const Point64& second) noexcept -> bool {
    return first.y == second.y;
}

auto get_segment_intersection(const Point64& first_start,
                              const Point64& first_end,
                              const Point64& second_start,
                              const Point64& second_end,
                              Point64& intersection) -> bool {
    const auto first_sign =
        cross_product_sign_in_clipper_range(first_start, second_start, second_end);
    const auto second_sign =
        cross_product_sign_in_clipper_range(first_end, second_start, second_end);
    if (first_sign == 0) {
        intersection = first_start;
        if (second_sign == 0) { return false; }
        if (first_start == second_start || first_start == second_end) { return true; }
        if (internal::is_horizontal(second_start, second_end)) {
            return (first_start.x > second_start.x) == (first_start.x < second_end.x);
        }
        return (first_start.y > second_start.y) == (first_start.y < second_end.y);
    }
    if (second_sign == 0) {
        intersection = first_end;
        if (first_end == second_start || first_end == second_end) { return true; }
        if (internal::is_horizontal(second_start, second_end)) {
            return (first_end.x > second_start.x) == (first_end.x < second_end.x);
        }
        return (first_end.y > second_start.y) == (first_end.y < second_end.y);
    }
    if ((first_sign > 0) == (second_sign > 0)) { return false; }

    const auto third_sign =
        cross_product_sign_in_clipper_range(second_start, first_start, first_end);
    const auto fourth_sign =
        cross_product_sign_in_clipper_range(second_end, first_start, first_end);
    if (third_sign == 0) {
        intersection = second_start;
        if (second_start == first_start || second_start == first_end) { return true; }
        if (internal::is_horizontal(first_start, first_end)) {
            return (second_start.x > first_start.x) == (second_start.x < first_end.x);
        }
        return (second_start.y > first_start.y) == (second_start.y < first_end.y);
    }
    if (fourth_sign == 0) {
        intersection = second_end;
        if (second_end == first_start || second_end == first_end) { return true; }
        if (internal::is_horizontal(first_start, first_end)) {
            return (second_end.x > first_start.x) == (second_end.x < first_end.x);
        }
        return (second_end.y > first_start.y) == (second_end.y < first_end.y);
    }
    if ((third_sign > 0) == (fourth_sign > 0)) { return false; }

    return line_intersection_point_in_clipper_range_fast(
        first_start, first_end, second_start, second_end, intersection);
}

auto get_intersection(const Rect64& rect,
                      const Point64& first,
                      const Point64& second,
                      rect_location& location,
                      Point64& intersection) -> bool {
    const auto top_left = Point64{rect.left, rect.top};
    const auto top_right = Point64{rect.right, rect.top};
    const auto bottom_right = Point64{rect.right, rect.bottom};
    const auto bottom_left = Point64{rect.left, rect.bottom};

    switch (location) {
    case rect_location::Left: {
        if (internal::get_segment_intersection(
                first, second, top_left, bottom_left, intersection)) {
            return true;
        }
        if ((first.y < rect.top) &&
            internal::get_segment_intersection(first, second, top_left, top_right, intersection)) {
            location = rect_location::Top;
            return true;
        }
        if (internal::get_segment_intersection(
                first, second, bottom_right, bottom_left, intersection)) {
            location = rect_location::Bottom;
            return true;
        }
        return false;
    }

    case rect_location::Top: {
        if (internal::get_segment_intersection(first, second, top_left, top_right, intersection)) {
            return true;
        }
        if ((first.x < rect.left) && internal::get_segment_intersection(
                                         first, second, top_left, bottom_left, intersection)) {
            location = rect_location::Left;
            return true;
        }
        if (internal::get_segment_intersection(
                first, second, top_right, bottom_right, intersection)) {
            location = rect_location::Right;
            return true;
        }
        return false;
    }

    case rect_location::Right: {
        if (internal::get_segment_intersection(
                first, second, top_right, bottom_right, intersection)) {
            return true;
        }
        if ((first.y < rect.top) &&
            internal::get_segment_intersection(first, second, top_left, top_right, intersection)) {
            location = rect_location::Top;
            return true;
        }
        if (internal::get_segment_intersection(
                first, second, bottom_right, bottom_left, intersection)) {
            location = rect_location::Bottom;
            return true;
        }
        return false;
    }

    case rect_location::Bottom: {
        if (internal::get_segment_intersection(
                first, second, bottom_right, bottom_left, intersection)) {
            return true;
        }
        if ((first.x < rect.left) && internal::get_segment_intersection(
                                         first, second, top_left, bottom_left, intersection)) {
            location = rect_location::Left;
            return true;
        }
        if (internal::get_segment_intersection(
                first, second, top_right, bottom_right, intersection)) {
            location = rect_location::Right;
            return true;
        }
        return false;
    }

    default: {
        if (internal::get_segment_intersection(
                first, second, top_left, bottom_left, intersection)) {
            location = rect_location::Left;
            return true;
        }
        if (internal::get_segment_intersection(first, second, top_left, top_right, intersection)) {
            location = rect_location::Top;
            return true;
        }
        if (internal::get_segment_intersection(
                first, second, top_right, bottom_right, intersection)) {
            location = rect_location::Right;
            return true;
        }
        if (internal::get_segment_intersection(
                first, second, bottom_right, bottom_left, intersection)) {
            location = rect_location::Bottom;
            return true;
        }
        return false;
    }
    }
}

}  // namespace clipper2next::internal
