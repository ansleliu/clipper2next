#pragma once

#include "triangulation/private/triangulation_context.h"

namespace clipper2next::internal {

auto add_triangulation_path(triangulation_context& context, const Path64& path) -> void;

[[nodiscard]] auto add_triangulation_paths(triangulation_context& context, const Paths64& paths)
    -> bool;

}  // namespace clipper2next::internal
