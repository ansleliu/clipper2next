#pragma once

#include "offset/private/offset_group.h"
#include "offset/private/offset_group_processor.h"
#include "offset/private/offset_state.h"
#include "clipper2next/core/path_set.h"

#include <span>

namespace clipper2next::internal {

auto offset_open_path(offset_state& state,
                      const offset_group& group,
                      std::span<const Point64> path,
                      const Path64& callback_path,
                      JoinType join_type,
                      EndType end_type,
                      double arc_tolerance,
                      std::size_t arc_segments_per_quadrant,
                      geotypes::CoordinateRounding coordinate_rounding,
                      delta_callback_ref delta_callback,
                      Paths64& output) -> void;

auto offset_open_path(offset_state& state,
                      const offset_group& group,
                      std::span<const Point64> path,
                      const Path64& callback_path,
                      JoinType join_type,
                      EndType end_type,
                      double arc_tolerance,
                      std::size_t arc_segments_per_quadrant,
                      geotypes::CoordinateRounding coordinate_rounding,
                      delta_callback_ref delta_callback,
                      path_set64& output) -> void;

}  // namespace clipper2next::internal
