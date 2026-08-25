#pragma once

#include "clip/engine/private/engine_state.h"
#include "clip/engine/private/engine_types.h"
#include "clipper2next/geometry/line_intersections.h"

namespace clipper2next::internal {

struct engine_intersection_services {
    predicate_policy intersection_policy{};
};

auto intersect_edges(clipper_base_state& state,
                     bool has_open_paths,
                     bool& succeeded,
                     active_edge_node& first,
                     active_edge_node& second,
                     const Point64& point) -> void;

}  // namespace clipper2next::internal
