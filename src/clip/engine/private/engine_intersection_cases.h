#pragma once

#include "clip/engine/private/engine_intersection_processor.h"

namespace clipper2next::internal {

[[nodiscard]] auto try_intersect_open_edges(clipper_base_state& state,
                                            bool has_open_paths,
                                            active_edge_node& first,
                                            active_edge_node& second,
                                            const Point64& point) -> bool;

auto intersect_closed_edges(clipper_base_state& state,
                            bool& succeeded,
                            active_edge_node& first,
                            active_edge_node& second,
                            const Point64& point) -> void;

}  // namespace clipper2next::internal
