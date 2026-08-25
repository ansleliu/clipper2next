#include "rectclip/private/rectclip_traversal.h"

#include <cstdlib>

namespace clipper2next::internal {

auto adjacent_location(rect_location location, bool clockwise) noexcept -> rect_location {
    const auto delta = clockwise ? 1 : 3;
    return static_cast<rect_location>((static_cast<int>(location) + delta) % 4);
}

auto heading_clockwise(rect_location previous, rect_location current) noexcept -> bool {
    return (static_cast<int>(previous) + 1) % 4 == static_cast<int>(current);
}

auto are_opposites(rect_location previous, rect_location current) noexcept -> bool {
    return std::abs(static_cast<int>(previous) - static_cast<int>(current)) == 2;
}

auto is_clockwise(rect_location previous,
                  rect_location current,
                  const Point64& previous_point,
                  const Point64& current_point,
                  const Point64& rect_midpoint) -> bool {
    if (internal::are_opposites(previous, current)) {
        return cross_product_sign(previous_point, rect_midpoint, current_point) < 0;
    }
    return internal::heading_clockwise(previous, current);
}

auto start_locations_are_clockwise(const std::vector<rect_location>& start_locations) -> bool {
    auto result = 0;
    for (size_t index = 1; index < start_locations.size(); ++index) {
        const auto delta =
            static_cast<int>(start_locations[index]) - static_cast<int>(start_locations[index - 1]);
        if (std::abs(delta) == 1) {
            result += delta;
        } else if (std::abs(delta) == 3) {
            result -= delta / 3;
        }
    }
    return result > 0;
}

auto next_external_location(const Rect64& rect,
                            const Path64& path,
                            rect_location location,
                            size_t& index,
                            size_t high_index) -> rect_location {
    switch (location) {
    case rect_location::Left: {
        while (index <= high_index && path[index].x <= rect.left) { ++index; }
        if (index > high_index) { break; }
        if (path[index].x >= rect.right) {
            location = rect_location::Right;
        } else if (path[index].y <= rect.top) {
            location = rect_location::Top;
        } else if (path[index].y >= rect.bottom) {
            location = rect_location::Bottom;
        } else {
            location = rect_location::Inside;
        }
        break;
    }

    case rect_location::Top: {
        while (index <= high_index && path[index].y <= rect.top) { ++index; }
        if (index > high_index) { break; }
        if (path[index].y >= rect.bottom) {
            location = rect_location::Bottom;
        } else if (path[index].x <= rect.left) {
            location = rect_location::Left;
        } else if (path[index].x >= rect.right) {
            location = rect_location::Right;
        } else {
            location = rect_location::Inside;
        }
        break;
    }

    case rect_location::Right: {
        while (index <= high_index && path[index].x >= rect.right) { ++index; }
        if (index > high_index) { break; }
        if (path[index].x <= rect.left) {
            location = rect_location::Left;
        } else if (path[index].y <= rect.top) {
            location = rect_location::Top;
        } else if (path[index].y >= rect.bottom) {
            location = rect_location::Bottom;
        } else {
            location = rect_location::Inside;
        }
        break;
    }

    case rect_location::Bottom: {
        while (index <= high_index && path[index].y >= rect.bottom) { ++index; }
        if (index > high_index) { break; }
        if (path[index].y <= rect.top) {
            location = rect_location::Top;
        } else if (path[index].x <= rect.left) {
            location = rect_location::Left;
        } else if (path[index].x >= rect.right) {
            location = rect_location::Right;
        } else {
            location = rect_location::Inside;
        }
        break;
    }

    default: {
        break;
    }
    }
    return location;
}

}  // namespace clipper2next::internal
