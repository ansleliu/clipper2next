#pragma once

#include "rectclip/private/rectclip_graph.h"

namespace clipper2next::internal {

[[nodiscard]] auto build_polygon_path(rectclip_node*& node) -> Path64;

[[nodiscard]] auto build_line_path(rectclip_node*& node) -> Path64;

}  // namespace clipper2next::internal
