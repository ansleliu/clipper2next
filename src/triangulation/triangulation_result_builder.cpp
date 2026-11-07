#include "triangulation/private/triangulation_result_builder.h"
#include "support/private/paths_rebuild.h"
#include <array>
#include <algorithm>

namespace clipper2next::internal {
namespace {
template <typename C>
auto project(const triangulation_mesh_view& mesh, Paths<C>& result, double scale) -> void {
    paths_rebuild<C> transaction{result};
    transaction.prepare(mesh.size(), [](auto) { return 3U; });
    transaction.begin();
    for (std::size_t i = 0; i < mesh.size(); ++i) {
        const auto& face = mesh.triangle(i);
        const auto vertices = triangle_vertices(face);
        std::array<Point64, 3> points{vertices[0]->pt, vertices[1]->pt, vertices[2]->pt};
        if (face.orientation < 0) { std::reverse(points.begin(), points.end()); }
        for (const auto& point : points) {
            if constexpr (std::is_same_v<C, int64_t>) { result[i].push_back(point); }
            else { result[i].emplace_back(point.x * scale, point.y * scale); }
        }
    }
    transaction.finish();
}
}  // namespace

auto build_triangulation_result_into(const triangulation_mesh_view& mesh, Paths64& result)
    -> void { project(mesh, result, 1.0); }
auto build_triangulation_result_into(const triangulation_mesh_view& mesh,
                                     PathsD& result, double scale) -> void {
    project(mesh, result, scale);
}
auto build_triangulation_result(const triangulation_mesh_view& mesh) -> Paths64 {
    Paths64 result;
    build_triangulation_result_into(mesh, result);
    return result;
}
}  // namespace clipper2next::internal
