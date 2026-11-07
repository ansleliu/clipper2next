#pragma once

#include "offset/private/offset_geometry.h"
#include "support/private/execution_resource_ledger.h"
#include "support/private/checked_size.h"

#include <cmath>
#include <optional>
#include <span>

namespace clipper2next::internal {

#if defined(_MSC_VER)
#define CLIPPER2NEXT_OFFSET_FORCE_INLINE __forceinline
#elif defined(__clang__) || defined(__GNUC__)
#define CLIPPER2NEXT_OFFSET_FORCE_INLINE inline __attribute__((always_inline))
#else
#define CLIPPER2NEXT_OFFSET_FORCE_INLINE inline
#endif

class offset_point_appender final {
public:
    offset_point_appender(Path64& output, scoped_workspace_charge* charge, std::size_t additional,
                          scoped_workspace_growth* phase = nullptr)
        : output_(output), growth_(phase) {
        if (!growth_) {
            growth_ = &local_.emplace(
                charge, checked_size_add(output.size(), additional), sizeof(Point64));
        }
    }
    CLIPPER2NEXT_OFFSET_FORCE_INLINE auto append(const Point64& point) -> void {
        output_.push_back(point);
        growth_->commit_slots(output_.size(), sizeof(Point64));
    }
private:
    Path64& output_;
    std::optional<scoped_workspace_growth> local_;
    scoped_workspace_growth* growth_;
};

inline auto append_perpendicular(Path64& output,
                                 const Point64& point,
                                 const PointD& normal,
                                 double delta,
                                 const offset_point_rounding& rounding = offset_point_rounding{},
                                 scoped_workspace_charge* charge = nullptr,
                                 scoped_workspace_growth* phase = nullptr) -> void {
    auto append = offset_point_appender{output, charge, 1U, phase};
    append.append(internal::perpendicular_point(point, normal, delta, rounding));
}

inline auto append_bevel(Path64& output,
                         std::span<const Point64> path,
                         const PathD& normals,
                         size_t vertex_index,
                         size_t previous_index,
                         double group_delta,
                         const offset_point_rounding& rounding = offset_point_rounding{},
                         scoped_workspace_charge* charge = nullptr,
                         scoped_workspace_growth* phase = nullptr) -> void {
    auto append = offset_point_appender{output, charge, 2U, phase};
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
    append.append(offset_point(first_point, rounding));
    append.append(offset_point(second_point, rounding));
}

inline auto append_square(Path64& output,
                          std::span<const Point64> path,
                          const PathD& normals,
                          size_t vertex_index,
                          size_t previous_index,
                          double group_delta,
                          const offset_point_rounding& rounding = offset_point_rounding{},
                          scoped_workspace_charge* charge = nullptr,
                          scoped_workspace_growth* phase = nullptr) -> void {
    auto append = offset_point_appender{output, charge, 2U, phase};
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
        append.append(offset_point(
            reflect_point(intersection, mid_point), rounding));
        append.append(offset_point(intersection, rounding));
    } else {
        const auto current_offset = internal::perpendicular_point_d(
            path[vertex_index], normals[previous_index], group_delta);
        auto intersection = mid_point;
        if (!line_intersection_point(
                first_line, second_line, previous_offset, current_offset, intersection)) {
            intersection = mid_point;
        }
        append.append(offset_point(intersection, rounding));
        append.append(offset_point(
            reflect_point(intersection, mid_point), rounding));
    }
}

CLIPPER2NEXT_OFFSET_FORCE_INLINE auto append_miter(Path64& output,
                         std::span<const Point64> path,
                         const PathD& normals,
                         size_t vertex_index,
                         size_t previous_index,
                         double group_delta,
                         double cos_a,
                         const offset_point_rounding& rounding = offset_point_rounding{},
                         scoped_workspace_charge* charge = nullptr,
                         scoped_workspace_growth* phase = nullptr) -> void {
    auto append = offset_point_appender{output, charge, 1U, phase};
    const auto q = group_delta / (cos_a + 1);
    append.append(offset_point({
        path[vertex_index].x + (normals[previous_index].x + normals[vertex_index].x) * q,
        path[vertex_index].y + (normals[previous_index].y + normals[vertex_index].y) * q}, rounding));
}

#undef CLIPPER2NEXT_OFFSET_FORCE_INLINE

inline auto append_round(Path64& output,
                         std::span<const Point64> path,
                         const PathD& normals,
                         size_t vertex_index,
                         size_t previous_index,
                         double group_delta,
                         const offset_arc_parameters& arc,
                         double angle,
                         const offset_point_rounding& rounding = offset_point_rounding{},
                         scoped_workspace_charge* charge = nullptr,
                         scoped_workspace_growth* phase = nullptr) -> void {
    const auto steps = arc_step_count(arc, angle);
    auto append = offset_point_appender{output, charge, checked_size_add(steps, 1U), phase};
    const auto point = path[vertex_index];
    auto offset_vector =
        PointD(normals[previous_index].x * group_delta, normals[previous_index].y * group_delta);
    if (vertex_index == previous_index) { offset_vector = negated(offset_vector); }

    append.append(offset_point({point.x + offset_vector.x, point.y + offset_vector.y}, rounding));
    for (std::size_t step = 1; step < steps; ++step) {
        offset_vector = PointD(offset_vector.x * arc.step_cos - arc.step_sin * offset_vector.y,
                               offset_vector.x * arc.step_sin + offset_vector.y * arc.step_cos);
        append.append(offset_point({point.x + offset_vector.x, point.y + offset_vector.y}, rounding));
    }
    append.append(internal::perpendicular_point(
        path[vertex_index], normals[vertex_index], group_delta, rounding));
}

}  // namespace clipper2next::internal
