#include "clip/engine/private/engine_intersection_processor.h"

#include "clip/engine/private/engine_intersection_cases.h"

namespace clipper2next::internal {

auto intersect_edges(clipper_base_state& state,
                     bool has_open_paths,
                     bool& succeeded,
                     active_edge_node& first,
                     active_edge_node& second,
                     const Point64& point) -> void {
    if (has_open_paths &&
        try_intersect_open_edges(state, has_open_paths, first, second, point)) {
        return;
    }
    intersect_closed_edges(state, succeeded, first, second, point);
}

}  // namespace clipper2next::internal
