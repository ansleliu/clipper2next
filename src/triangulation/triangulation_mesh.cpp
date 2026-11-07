#include "triangulation/private/triangulation_mesh.h"
#include "triangulation/private/triangulation_context.h"
#include "triangulation/private/triangulation_topology.h"
#include "geometry/private/geometry_predicates.h"
#include "clipper2next/api/error.h"

namespace clipper2next::internal {
namespace {
auto invalid_mesh() -> void { raise_clipper_error(clipper_error_code::internal_error); }
auto connects(const triangulation_edge& side, triangulation_vertex_ref first,
              triangulation_vertex_ref second) noexcept -> bool {
    return (side.first == first && side.second == second) ||
           (side.first == second && side.second == first);
}
auto validate_face(triangulation_triangle& face, const triangulation_predicate_domain& domain) -> void {
    if (face.orientation) { invalid_mesh(); }
    const auto vertices = triangle_vertices(face);
    face.orientation = static_cast<int8_t>(domain.orientation(
        vertices[0]->pt, vertices[1]->pt, vertices[2]->pt));
    if (!face.orientation) { invalid_mesh(); }
    const auto a = vertices[0], b = vertices[1], c = vertices[2];
    // A nondegenerate face has exactly the pairs AB, AC and BC. Requiring
    // each once proves closure and excludes duplicate objects and endpoints.
    if (!connects(*face.edges[0], a, b)) { invalid_mesh(); }
    const auto second_from_a = connects(*face.edges[1], a, c);
    if (!second_from_a && !connects(*face.edges[1], b, c)) { invalid_mesh(); }
    if (!connects(*face.edges[2], second_from_a ? b : a, c)) { invalid_mesh(); }
    for (const auto side : face.edges) {
        if (side->triA.get() != &face && side->triB.get() != &face) { invalid_mesh(); }
    }
}
auto owned_side_direction(const triangulation_triangle& face,
                          const triangulation_edge& side) noexcept -> int {
    if (!face.orientation) { return 0; }
    if (face.edges[0].get() == &side) {
        return side.first == side.vL ? face.orientation : -face.orientation;
    }
    if (face.edges[1].get() != &side && face.edges[2].get() != &side) { return 0; }
    // validate_face already proved three distinct corners and a closed cycle.
    // Relative to its first canonical side (a,b), the other sides are (b,c)
    // and (c,a). Their direction needs no second corner reconstruction.
    const auto& base = *face.edges[0];
    return side.first == base.vR || side.second == base.vL
        ? face.orientation : -face.orientation;
}

auto validate_side(const triangulation_edge& edge) -> void {
    if (!edge.triA || (is_loose_edge(edge) != static_cast<bool>(edge.triB)) ||
        (edge.triB && edge.triA == edge.triB)) { invalid_mesh(); }
    const auto first = owned_side_direction(*edge.triA, edge);
    if (!first || (!is_loose_edge(edge) && first < 0)) { invalid_mesh(); }
    if (edge.triB) {
        const auto second = owned_side_direction(*edge.triB, edge);
        if (!second || first == second) { invalid_mesh(); }
    }
}
}  // namespace

auto validate_triangulation_mesh(triangulation_context& context,
                                  const triangulation_predicate_domain& domain) -> triangulation_mesh_view {
    if (!context.pending_delaunay.empty()) { invalid_mesh(); }
    if (context.triangles.empty()) {
        if (!context.edges.empty()) { invalid_mesh(); }
        return {};
    }
    // Reset every associated face, including an edge reference absent from the
    // listed output faces. This also makes duplicate listed refs detectable.
    for (auto edge : context.edges) {
        if (edge->triA) { edge->triA->orientation = 0; }
        if (edge->triB) { edge->triB->orientation = 0; }
    }
    for (auto face : context.triangles) { validate_face(*face, domain); }
    for (auto edge : context.edges) { validate_side(*edge); }
    return triangulation_mesh_view{std::span<const triangulation_triangle_ref>{
        context.triangles.data(), context.triangles.size()}};
}
}  // namespace clipper2next::internal
