#pragma once

#include "triangulation/private/triangulation_edge_graph.h"
#include "triangulation/private/triangulation_catalog.h"
#include "support/private/storage/stable_block_storage.h"
#include "triangulation/private/triangulation_types.h"
#include "triangulation/private/triangulation_exact_predicates.h"

#include "triangulation/private/triangulation_monotone.h"
#include "triangulation/private/triangulation_path_builder.h"

#include <array>
#include <memory_resource>
#include <vector>

namespace clipper2next::internal {

// Pipeline invariant: all vertex coordinates have passed the public range admission
// (or are bounded fixtures in internal tests). Graph operations reuse these points.
struct triangulation_context final {
    std::pmr::monotonic_buffer_resource edge_scratch;
    // Graph-owned node storage has no execution-resource-ledger binding.
    stable_block_storage<triangulation_vertex> vertex_pool;
    stable_block_storage<triangulation_edge> edge_pool;
    stable_block_storage<triangulation_triangle> triangle_pool;
    triangulation_vertex_list vertices;
    triangulation_edge_list edges;
    triangulation_triangle_list triangles;
    triangulation_edge_list pending_delaunay;
    monotone_workspace monotone;
    // Face traversal belongs to the retained graph operation, just like its
    // monotone ordering. Keep its directories between calls; release() owns
    // returning their storage along with the rest of this context.
    triangulation_vertex_list partition_vertices;
    Path64 partition_path;
    std::vector<triangulation_edge_ref> partition_sides;
    // A certified Sweep face needs its ordered corners, not mutable CDT edges.
    std::vector<std::array<triangulation_vertex_ref, 3>> partition_faces;
    triangulation_source_paths source_paths;
    std::vector<boundary_corner_event> boundary_events;
    std::vector<std::size_t> local_minima;
    triangulation_bounds_catalog source_rings;
    triangulation_edge_ref first_active;
    bool use_delaunay = true;
    bool internal_error = false;

    auto clear() noexcept -> void;
    auto release() noexcept -> void;
};

inline auto mark_triangulation_internal_error(triangulation_context& context) noexcept -> void {
    context.internal_error = true;
}

[[nodiscard]] auto prepare_triangulation_predicate_domain(const triangulation_context& context)
    -> triangulation_predicate_domain;

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
    // Graph production schedules the optional phase only when requested.
    if (context.use_delaunay) { context.pending_delaunay.push_back(edge); }
}

}  // namespace clipper2next::internal
