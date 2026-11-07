#include "triangulation/private/triangulation_context.h"
#include "support/private/storage/release_vector.h"

namespace clipper2next::internal {

auto prepare_triangulation_predicate_domain(const triangulation_context& context)
    -> triangulation_predicate_domain {
    return triangulation_predicate_domain::from_points(context.vertices.size(),
        [&](std::size_t i) { return context.vertices[i]->pt; });
}

auto triangulation_context::clear() noexcept -> void {
    source_rings.clear();
    monotone.clear();
    partition_vertices.clear();
    partition_path.clear();
    partition_sides.clear();
    partition_faces.clear();
    vertex_pool.clear();
    edge_scratch.release();
    edge_pool.clear();
    triangle_pool.clear();
    vertices.clear();
    edges.clear();
    triangles.clear();
    pending_delaunay.clear();
    source_paths.clear();
    boundary_events.clear();
    local_minima.clear();
    first_active = nullptr;
    internal_error = false;
}

auto triangulation_context::release() noexcept -> void {
    clear();
    // Return auxiliary directories before their cleared backing node storage.
    release_vector(vertices);
    release_vector(triangles);
    release_vector(local_minima);
    release_vector(partition_vertices);
    release_vector(partition_path);
    release_vector(partition_sides);
    release_vector(partition_faces);
    source_paths.release();
    release_vector(boundary_events);
    release_vector(edges);
    release_vector(pending_delaunay);
    source_rings.release();
    monotone.release();
    vertex_pool.release();
    edge_pool.release();
    triangle_pool.release();
}

auto create_triangulation_vertex(triangulation_context& context, const Point64& point)
    -> triangulation_vertex* {
    auto& vertex = context.vertex_pool.emplace(point, &context.edge_scratch);
    auto* raw_vertex = &vertex;
    context.vertices.emplace_back(raw_vertex);
    return raw_vertex;
}

auto create_triangulation_edge(triangulation_context& context,
                               triangulation_vertex* first,
                               triangulation_vertex* second,
                               triangulation_edge_kind kind) -> triangulation_edge* {
    auto& edge = context.edge_pool.emplace();
    initialize_triangulation_edge(edge, first, second, kind);
    auto* raw_edge = &edge;
    context.edges.emplace_back(raw_edge);
    first->edges.push_back(raw_edge);
    second->edges.push_back(raw_edge);
    return raw_edge;
}

auto create_triangulation_triangle(triangulation_context& context,
                                   triangulation_edge* first,
                                   triangulation_edge* second,
                                   triangulation_edge* third) -> triangulation_triangle* {
    auto& triangle = context.triangle_pool.emplace(first, second, third);
    auto* raw_triangle = &triangle;
    context.triangles.emplace_back(raw_triangle);
    return raw_triangle;
}

}  // namespace clipper2next::internal
