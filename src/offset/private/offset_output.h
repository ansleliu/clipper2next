#pragma once

#include "offset/private/offset_geometry.h"

#include <cmath>
#include <span>

namespace clipper2next::internal {

inline auto append_perpendicular(Path64& output,
                                 const Point64& point,
                                 const PointD& normal,
                                 double delta,
                                 const geotypes::CoordinateRounding rounding =
                                     geotypes::CoordinateRounding::NearestEven) -> void {
    output.emplace_back(internal::perpendicular_point(point, normal, delta, rounding));
}

inline auto append_bevel(Path64& output,
                         std::span<const Point64> path,
                         const PathD& normals,
                         size_t vertex_index,
                         size_t previous_index,
                         double group_delta,
                         const geotypes::CoordinateRounding rounding =
                             geotypes::CoordinateRounding::NearestEven) -> void {
    PointD first_point;
    PointD second_point;
    if (vertex_index == previous_index) {
        const auto abs_delta = std::abs(group_delta);
        first_point = PointD(path[vertex_index].x - abs_delta * normals[vertex_index].x,
                             path[vertex_index].y - abs_delta * normals[vertex_index].y);
        second_point = PointD(path[vertex_index].x + abs_delta * normals[vertex_index].x,
                              path[vertex_index].y + abs_delta * normals[vertex_index].y);
    } else {
        first_point = PointD(path[vertex_index].x + group_delta * normals[previous_index].x,
                             path[vertex_index].y + group_delta * normals[previous_index].y);
        second_point = PointD(path[vertex_index].x + group_delta * normals[vertex_index].x,
                              path[vertex_index].y + group_delta * normals[vertex_index].y);
    }
    output.emplace_back(geotypes::pointCast<std::int64_t>(first_point, rounding));
    output.emplace_back(geotypes::pointCast<std::int64_t>(second_point, rounding));
}

inline auto append_square(Path64& output,
                          std::span<const Point64> path,
                          const PathD& normals,
                          size_t vertex_index,
                          size_t previous_index,
                          double group_delta,
                          const geotypes::CoordinateRounding rounding =
                              geotypes::CoordinateRounding::NearestEven) -> void {
    const auto vector = vertex_index == previous_index
                            ? PointD(normals[vertex_index].y, -normals[vertex_index].x)
                            : internal::average_unit_vector(
                                  PointD(-normals[previous_index].y, normals[previous_index].x),
                                  PointD(normals[vertex_index].y, -normals[vertex_index].x));

    const auto abs_delta = std::abs(group_delta);
    auto mid_point = geotypes::pointCast<double>(path[vertex_index]);
    mid_point = translate_point(mid_point, abs_delta * vector.x, abs_delta * vector.y);
    const auto first_line =
        translate_point(mid_point, group_delta * vector.y, group_delta * -vector.x);
    const auto second_line =
        translate_point(mid_point, group_delta * -vector.y, group_delta * vector.x);
    const auto previous_offset =
        internal::perpendicular_point_d(path[previous_index], normals[previous_index], group_delta);
    if (vertex_index == previous_index) {
        const auto previous_edge = PointD(previous_offset.x + vector.x * group_delta,
                                          previous_offset.y + vector.y * group_delta);
        auto intersection = mid_point;
        if (!line_intersection_point(
                first_line, second_line, previous_offset, previous_edge, intersection)) {
            intersection = mid_point;
        }
        output.emplace_back(geotypes::pointCast<std::int64_t>(
            reflect_point(intersection, mid_point), rounding));
        output.emplace_back(geotypes::pointCast<std::int64_t>(intersection, rounding));
    } else {
        const auto current_offset = internal::perpendicular_point_d(
            path[vertex_index], normals[previous_index], group_delta);
        auto intersection = mid_point;
        if (!line_intersection_point(
                first_line, second_line, previous_offset, current_offset, intersection)) {
            intersection = mid_point;
        }
        output.emplace_back(geotypes::pointCast<std::int64_t>(intersection, rounding));
        output.emplace_back(geotypes::pointCast<std::int64_t>(
            reflect_point(intersection, mid_point), rounding));
    }
}

inline auto append_miter(Path64& output,
                         std::span<const Point64> path,
                         const PathD& normals,
                         size_t vertex_index,
                         size_t previous_index,
                         double group_delta,
                         double cos_a,
                         const geotypes::CoordinateRounding rounding =
                             geotypes::CoordinateRounding::NearestEven) -> void {
    const auto q = group_delta / (cos_a + 1);
    output.emplace_back(geotypes::Point2i64{
        geotypes::coordinateCast<std::int64_t>(
            path[vertex_index].x +
            (normals[previous_index].x + normals[vertex_index].x) * q,
            rounding),
        geotypes::coordinateCast<std::int64_t>(
            path[vertex_index].y +
            (normals[previous_index].y + normals[vertex_index].y) * q,
            rounding)});
}

inline auto append_round(Path64& output,
                         std::span<const Point64> path,
                         const PathD& normals,
                         size_t vertex_index,
                         size_t previous_index,
                         double group_delta,
                         const offset_arc_parameters& arc,
                         double angle,
                         const geotypes::CoordinateRounding rounding =
                             geotypes::CoordinateRounding::NearestEven) -> void {
    const auto point = path[vertex_index];
    auto offset_vector =
        PointD(normals[previous_index].x * group_delta, normals[previous_index].y * group_delta);
    if (vertex_index == previous_index) { offset_vector = negated(offset_vector); }

    output.emplace_back(geotypes::Point2i64{
        geotypes::coordinateCast<std::int64_t>(point.x + offset_vector.x, rounding),
        geotypes::coordinateCast<std::int64_t>(point.y + offset_vector.y, rounding)});
    const auto steps = arc_step_count(arc, angle);
    for (std::size_t step = 1; step < steps; ++step) {
        offset_vector = PointD(offset_vector.x * arc.step_cos - arc.step_sin * offset_vector.y,
                               offset_vector.x * arc.step_sin + offset_vector.y * arc.step_cos);
        output.emplace_back(geotypes::Point2i64{
            geotypes::coordinateCast<std::int64_t>(point.x + offset_vector.x, rounding),
            geotypes::coordinateCast<std::int64_t>(point.y + offset_vector.y, rounding)});
    }
    internal::append_perpendicular(
        output, path[vertex_index], normals[vertex_index], group_delta, rounding);
}

}  // namespace clipper2next::internal
