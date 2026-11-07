#include "triangulation/private/triangulation_boundary.h"

#include "triangulation/private/triangulation_path_builder.h"
#include "triangulation/private/triangulation_graph_repair.h"
#include "triangulation/private/triangulation_boundary_events.h"

namespace clipper2next::internal {

auto build_triangulation_boundary(triangulation_context& context, const Paths64& paths)
    -> TriangulateResult {
    if (!context.source_paths.append(paths)) { return TriangulateResult::no_polygons; }
    const auto result = normalize_triangulation_constraints(context);
    if (result == TriangulateResult::success) { build_boundary_events(context); }
    return result;
}

}  // namespace clipper2next::internal
