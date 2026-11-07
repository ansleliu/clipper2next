#pragma once

#include "offset/private/offset_group_processor.h"

namespace clipper2next::internal {
// Generates one contour in path_out; the caller owns its transfer to raw output.
auto build_single_point_offset(offset_state& state,
                               const offset_group& group,
                               Point64 point,
                               const Path64& callback_path,
                               const offset_group_execution_options& options,
                               delta_callback_ref delta_callback) -> void;
}  // namespace clipper2next::internal
