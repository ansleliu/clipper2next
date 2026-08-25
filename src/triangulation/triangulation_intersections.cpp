#include "triangulation/private/triangulation_intersections.h"

namespace clipper2next::internal {

auto ShortestDistFromSegment(const Point64& point,
                             const Point64& segment_start,
                             const Point64& segment_end) -> double {
    const double dx = static_cast<double>(segment_end.x - segment_start.x);
    const double dy = static_cast<double>(segment_end.y - segment_start.y);
    const double ax = static_cast<double>(point.x - segment_start.x);
    const double ay = static_cast<double>(point.y - segment_start.y);
    const double q_num = ax * dx + ay * dy;
    if (q_num < 0) { return distance_squared(point, segment_start); }
    if (q_num > (square(dx) + square(dy))) { return distance_squared(point, segment_end); }
    return square(ax * dy - dx * ay) / (dx * dx + dy * dy);
}

auto SegsIntersect(Point64 segment1_start,
                   Point64 segment1_end,
                   Point64 segment2_start,
                   Point64 segment2_end) -> triangulation_intersect_kind {
    if (segment1_start == segment2_start || segment1_end == segment2_start ||
        segment1_end == segment2_end) {
        return triangulation_intersect_kind::none;
    }

    const double dy1 = static_cast<double>(segment1_end.y - segment1_start.y);
    const double dx1 = static_cast<double>(segment1_end.x - segment1_start.x);
    const double dy2 = static_cast<double>(segment2_end.y - segment2_start.y);
    const double dx2 = static_cast<double>(segment2_end.x - segment2_start.x);
    const double cp = dy1 * dx2 - dy2 * dx1;
    if (cp == 0) { return triangulation_intersect_kind::collinear; }

    double t = (static_cast<double>(segment1_start.x - segment2_start.x) * dy2 -
                static_cast<double>(segment1_start.y - segment2_start.y) * dx2);
    if (t >= 0) {
        if (cp < 0 || t >= cp) { return triangulation_intersect_kind::none; }
    } else if (cp > 0 || t <= cp) {
        return triangulation_intersect_kind::none;
    }

    t = ((segment1_start.x - segment2_start.x) * dy1 - (segment1_start.y - segment2_start.y) * dx1);
    if (t >= 0) {
        if (cp > 0 && t < cp) { return triangulation_intersect_kind::intersect; }
    } else if (cp < 0 && t > cp) {
        return triangulation_intersect_kind::intersect;
    }
    return triangulation_intersect_kind::none;
}

}  // namespace clipper2next::internal
