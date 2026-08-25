#pragma once

#include "offset/private/offset_geometry.h"

#include <cstddef>

namespace clipper2next::internal {

[[nodiscard]] auto estimate_path_output_capacity(size_t path_size,
                                                 JoinType join_type,
                                                 EndType end_type,
                                                 const offset_arc_parameters& arc) noexcept
    -> size_t;

[[nodiscard]] auto estimate_single_point_output_capacity(JoinType join_type,
                                                         const offset_arc_parameters& arc) noexcept
    -> size_t;

}  // namespace clipper2next::internal
