#pragma once

#include "clipper2next/core.h"
#include "clipper2next/offset/types.h"

namespace clipper2next::internal {

struct offset_state;

[[nodiscard]] auto inflate_paths_with_state(offset_state& state,
                                            const Paths64& paths,
                                            double delta,
                                            JoinType join_type,
                                            EndType end_type,
                                            double miter_limit,
                                            double arc_tolerance) -> Paths64;

}  // namespace clipper2next::internal
