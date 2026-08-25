#include "triangulation/private/triangulation_boundary.h"

#include "triangulation/private/triangulation_path_builder.h"

namespace clipper2next::internal {

auto build_triangulation_boundary(triangulation_context& context, const Paths64& paths) -> bool {
    return add_triangulation_paths(context, paths);
}

}  // namespace clipper2next::internal
