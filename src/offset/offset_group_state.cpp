#include "offset/private/offset_group_processor.h"

#include "offset/private/offset_geometry.h"

#include <cmath>

namespace clipper2next::internal {

auto prepare_offset_group_state(offset_state& state,
                                const offset_group& group,
                                const offset_group_execution_options& options,
                                const double operation_delta) -> void {
    if (group.end_type == EndType::Polygon) {
        state.group_delta = group.is_reversed ? -operation_delta : operation_delta;
    } else {
        state.group_delta = std::abs(operation_delta);
    }

    state.temp_limit =
        options.miter_limit <= 1.0 ? 2.0 : 2.0 / (options.miter_limit * options.miter_limit);
    if (group.join_type == JoinType::Round || group.end_type == EndType::Round) {
        state.arc = make_arc_parameters(
            state.group_delta,
            options.arc_tolerance,
            options.arc_segments_per_quadrant);
    }
}

}  // namespace clipper2next::internal
