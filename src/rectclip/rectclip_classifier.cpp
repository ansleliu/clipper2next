#include "rectclip/private/rectclip_classifier.h"

namespace clipper2next::internal {

auto classify_point(const Rect64& rect, const Point64& point) -> rect_location {
    if (point.x == rect.left && point.y >= rect.top && point.y <= rect.bottom) {
        return rect_location::Left;
    }
    if (point.x == rect.right && point.y >= rect.top && point.y <= rect.bottom) {
        return rect_location::Right;
    }
    if (point.y == rect.top && point.x >= rect.left && point.x <= rect.right) {
        return rect_location::Top;
    }
    if (point.y == rect.bottom && point.x >= rect.left && point.x <= rect.right) {
        return rect_location::Bottom;
    }
    if (point.x < rect.left) { return rect_location::Left; }
    if (point.x > rect.right) { return rect_location::Right; }
    if (point.y < rect.top) { return rect_location::Top; }
    if (point.y > rect.bottom) { return rect_location::Bottom; }
    return rect_location::Inside;
}

auto is_on_rect_boundary(const Rect64& rect, const Point64& point) -> bool {
    return (point.x == rect.left && point.y >= rect.top && point.y <= rect.bottom) ||
           (point.x == rect.right && point.y >= rect.top && point.y <= rect.bottom) ||
           (point.y == rect.top && point.x >= rect.left && point.x <= rect.right) ||
           (point.y == rect.bottom && point.x >= rect.left && point.x <= rect.right);
}

}  // namespace clipper2next::internal
