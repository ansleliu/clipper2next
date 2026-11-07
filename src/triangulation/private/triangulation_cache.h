#pragma once

#include "clipper2next/triangulation/request.h"
#include "triangulation/private/triangulation_mesh.h"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace clipper2next::internal {
class triangulation_operation_lease;

struct triangulation_cache_entry final {
    std::uint64_t hash{};
    std::size_t path_count{};
    std::size_t point_count{};
    std::size_t cached_units{};
    bool use_delaunay{};
    Paths64 request_paths{};
    triangulation_result64 result{};
    std::uint64_t last_used_tick{};
};

struct triangulation_cache_state final {
    std::vector<triangulation_cache_entry> entries{};
    std::size_t total_units{};
    std::uint64_t tick{};
    // The last entry can use the lease's validated graph until it needs an
    // independent value. Older entries always own their triangles.
    triangulation_mesh_view pending_graph{};

    auto materialize_graph_entry() -> void;
    auto release() noexcept -> void;
};

// The mesh belongs to the operation's graph.
auto store_cached_triangulation_graph(triangulation_operation_lease&,
                                      const triangulation_request64&,
                                      TriangulateResult, const triangulation_mesh_view&) -> void;

[[nodiscard]] auto try_get_cached_triangulation(triangulation_operation_lease&,
                                                const triangulation_request64& request,
                                                triangulation_result64& result) -> bool;
[[nodiscard]] auto try_get_cached_triangulation(triangulation_operation_lease&,
                                                const triangulation_request64&,
                                                triangulation_resultd&,
                                                double) -> bool;

}  // namespace clipper2next::internal
