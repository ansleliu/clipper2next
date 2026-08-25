#include "triangulation/private/triangulation_context.h"

namespace clipper2next::internal {

auto triangulation_context::clear() -> void {
    vertex_pool.clear();
    edge_scratch.release();
    edge_pool.clear();
    triangle_pool.clear();
    vertices.clear();
    edges.clear();
    triangles.clear();
    pending_delaunay.clear();
    horizontal_edges.clear();
    local_minima.clear();
    lowermost_vertex = nullptr;
    first_active = nullptr;
    internal_error = false;
}

auto triangulation_context::release() noexcept -> void {
    clear();
    vertex_pool.release();
    edge_pool.release();
    triangle_pool.release();

    triangulation_vertex_list{}.swap(vertices);
    triangulation_triangle_list{}.swap(triangles);
    triangulation_vertex_list{}.swap(local_minima);
    triangulation_edge_list{edges.get_allocator().resource()}.swap(edges);
    triangulation_edge_list{pending_delaunay.get_allocator().resource()}.swap(pending_delaunay);
    triangulation_edge_list{horizontal_edges.get_allocator().resource()}.swap(horizontal_edges);
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
