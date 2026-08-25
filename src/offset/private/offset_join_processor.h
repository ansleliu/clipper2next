#pragma once

#include <cstddef>
#include <span>

#include "offset/private/offset_group.h"
#include "offset/private/offset_state.h"
#include "clipper2next/offset/types.h"

namespace clipper2next::internal {

struct offset_join_options final {
    JoinType join_type{JoinType::Bevel};
    double arc_tolerance{0.0};
    std::size_t arc_segments_per_quadrant{};
    geotypes::CoordinateRounding coordinate_rounding{
        geotypes::CoordinateRounding::NearestEven};
};

auto append_offset_join(offset_state& state,
                        const offset_group& group,
                        std::span<const Point64> path,
                        const Path64& callback_path,
                        std::size_t current_index,
                        std::size_t previous_index,
                        const offset_join_options& options,
                        delta_callback_ref delta_callback) -> void;

}  // namespace clipper2next::internal
