#pragma once

#include "triangulation/private/triangulation_edge_graph.h"
#include "support/private/storage/stable_pool.h"
#include "triangulation/private/triangulation_types.h"

#include <memory_resource>
#include <vector>

namespace clipper2next::internal {

struct triangulation_context final {
    std::pmr::monotonic_buffer_resource edge_scratch;
    stable_pool<triangulation_vertex> vertex_pool;
    stable_pool<triangulation_edge> edge_pool;
    stable_pool<triangulation_triangle> triangle_pool;
    triangulation_vertex_list vertices;
    triangulation_edge_list edges;
    triangulation_triangle_list triangles;
    triangulation_edge_list pending_delaunay;
    triangulation_edge_list horizontal_edges;
    triangulation_vertex_list local_minima;
    triangulation_vertex_ref lowermost_vertex;
    triangulation_edge_ref first_active;
    bool use_delaunay = true;
    bool internal_error = false;

    auto clear() -> void;
    auto release() noexcept -> void;
};

inline auto mark_triangulation_internal_error(triangulation_context& context) noexcept -> void {
    context.internal_error = true;
}

[[nodiscard]] auto create_triangulation_vertex(triangulation_context& context, const Point64& point)
    -> triangulation_vertex*;

[[nodiscard]] auto create_triangulation_edge(
    triangulation_context& context,
    triangulation_vertex* first,
    triangulation_vertex* second,
    triangulation_edge_kind kind = triangulation_edge_kind::loose) -> triangulation_edge*;

[[nodiscard]] auto create_triangulation_triangle(triangulation_context& context,
                                                 triangulation_edge* first,
                                                 triangulation_edge* second,
                                                 triangulation_edge* third)
    -> triangulation_triangle*;

inline auto enqueue_pending_delaunay(triangulation_context& context, triangulation_edge* edge)
    -> void {
    context.pending_delaunay.push_back(edge);
}

}  // namespace clipper2next::internal
