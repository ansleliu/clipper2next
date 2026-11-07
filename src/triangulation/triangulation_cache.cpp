#include "triangulation/private/triangulation_cache.h"
#include "support/private/storage/release_vector.h"
#include "triangulation/private/triangulation_operation.h"

#include "support/private/checked_size.h"

#include "support/private/paths_rebuild.h"
#include "triangulation/private/triangulation_result_builder.h"
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace clipper2next::internal {
namespace {

inline constexpr std::size_t minimum_cached_point_count = 512U;
inline constexpr std::size_t entry_capacity = 256U;
inline constexpr std::size_t maximum_entry_units = 64U * 1024U;
inline constexpr std::size_t total_unit_budget = 512U * 1024U;

auto hash_mix(std::uint64_t& seed, std::uint64_t value) -> void {
    seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U);
}

[[nodiscard]] auto count_points(const Paths64& paths) -> std::size_t {
    std::size_t total = 0;
    for (const auto& path : paths) { total = checked_size_add(total, path.size()); }
    return total;
}

[[nodiscard]] auto count_cache_units(const Paths64& paths) -> std::size_t {
    return checked_size_add(paths.size(), count_points(paths));
}

[[nodiscard]] auto hash_request(const triangulation_request64& request) -> std::uint64_t {
    std::uint64_t x_hash = request.use_delaunay ? 0x0123456789abcdefULL : 0xfedcba9876543210ULL;
    std::uint64_t y_hash = 0x9e3779b97f4a7c15ULL;
    hash_mix(x_hash, static_cast<std::uint64_t>(request.paths.size()));
    hash_mix(y_hash, static_cast<std::uint64_t>(request.paths.size()));
    for (const auto& path : request.paths) {
        hash_mix(x_hash, static_cast<std::uint64_t>(path.size()));
        hash_mix(y_hash, static_cast<std::uint64_t>(path.size()));
        for (const auto& point : path) {
            // Coordinate streams are independent; avoid one serialized chain
            // for both. Hashes only filter: a hit still compares every point.
            hash_mix(x_hash, static_cast<std::uint64_t>(point.x));
            hash_mix(y_hash, static_cast<std::uint64_t>(point.y));
        }
    }
    hash_mix(x_hash, y_hash);
    return x_hash;
}

[[nodiscard]] auto paths_exact_equal(const Paths64& left, const Paths64& right) -> bool {
    if (left.size() != right.size()) { return false; }
    for (std::size_t path_index = 0; path_index < left.size(); ++path_index) {
        const auto& first = left[path_index];
        const auto& second = right[path_index];
        if (first.size() != second.size()) { return false; }
        for (std::size_t point_index = 0; point_index < first.size(); ++point_index) {
            if (first[point_index] != second[point_index]) { return false; }
        }
    }
    return true;
}

auto evict_least_recently_used(triangulation_cache_state& state) -> void {
    auto oldest = state.entries.begin();
    for (auto entry = state.entries.begin() + 1; entry != state.entries.end(); ++entry) {
        if (entry->last_used_tick < oldest->last_used_tick) { oldest = entry; }
    }
    state.total_units -= oldest->cached_units;
    *oldest = std::move(state.entries.back());
    state.entries.pop_back();
}

auto find_cached_triangulation(triangulation_operation_lease& operation,
                              const triangulation_request64& request)
    -> const triangulation_result64* {
    if (!request.use_delaunay) { return nullptr; }
    auto* state = operation.existing_cache();
    if (!state || state->entries.empty()) { return nullptr; }
    if (state->entries.size() == 1U) {
        // With one entry, its exact admitted key is the only comparison needed.
        auto& entry = state->entries.front();
        if (!paths_exact_equal(entry.request_paths, request.paths)) { return nullptr; }
        state->materialize_graph_entry();
        entry.last_used_tick = ++state->tick;
        return &entry.result;
    }
    const auto request_point_count = count_points(request.paths);
    if (request_point_count < minimum_cached_point_count ||
        checked_size_add(request.paths.size(), request_point_count) > maximum_entry_units) {
        return nullptr;
    }

    const auto request_hash = hash_request(request);
    for (auto& entry : state->entries) {
        if (entry.hash != request_hash || entry.path_count != request.paths.size() ||
            entry.point_count != request_point_count ||
            entry.use_delaunay != request.use_delaunay ||
            !paths_exact_equal(entry.request_paths, request.paths)) {
            continue;
        }
        if (&entry == &state->entries.back()) { state->materialize_graph_entry(); }
        entry.last_used_tick = ++state->tick;
        return &entry.result;
    }
    return nullptr;
}

}  // namespace

auto try_get_cached_triangulation(triangulation_operation_lease& operation,
                                  const triangulation_request64& request,
                                  triangulation_result64& result) -> bool {
    const auto* cached = find_cached_triangulation(operation, request);
    if (!cached) { return false; }
    materialize_paths(cached->triangles, result.triangles);
    result.status = cached->status;
    return true;
}
auto try_get_cached_triangulation(triangulation_operation_lease& operation,
                                  const triangulation_request64& request,
                                  triangulation_resultd& result,
                                  double inverse_scale) -> bool {
    const auto* cached = find_cached_triangulation(operation, request);
    if (!cached) { return false; }
    paths_rebuild<double> transaction{result.triangles};
    transaction.prepare(cached->triangles.size(),
                        [&](auto i) { return cached->triangles[i].size(); });
    transaction.begin();
    for (std::size_t i = 0; i < cached->triangles.size(); ++i) {
        for (const auto& point : cached->triangles[i]) {
            result.triangles[i].emplace_back(point.x * inverse_scale, point.y * inverse_scale);
        }
    }
    transaction.finish();
    result.status = cached->status;
    return true;
}

auto store_cached_triangulation_graph(triangulation_operation_lease& operation,
                                      const triangulation_request64& request,
                                      TriangulateResult status, const triangulation_mesh_view& mesh) -> void {
    if (status != TriangulateResult::success) { return; }
    if (mesh.size() == 0 ||
        !request.use_delaunay || count_points(request.paths) < minimum_cached_point_count) {
        return;
    }
    const auto units = checked_size_add(count_cache_units(request.paths),
                                        checked_size_multiply(mesh.size(), 4U));
    if (units > maximum_entry_units) { return; }
    auto& state = operation.cache();
    state.materialize_graph_entry();
    while (!state.entries.empty() && (state.entries.size() >= entry_capacity ||
                                      units > total_unit_budget - state.total_units)) {
        evict_least_recently_used(state);
    }
    triangulation_cache_entry entry;
    entry.hash = hash_request(request);
    entry.path_count = request.paths.size();
    entry.point_count = count_points(request.paths);
    entry.cached_units = units;
    entry.use_delaunay = request.use_delaunay;
    entry.request_paths = request.paths;
    entry.result.status = status;
    entry.last_used_tick = ++state.tick;
    state.entries.push_back(std::move(entry));
    state.pending_graph = mesh;
    state.total_units = checked_size_add(state.total_units, units);
}

auto triangulation_cache_state::materialize_graph_entry() -> void {
    if (pending_graph.size() == 0U) { return; }
    build_triangulation_result_into(pending_graph, entries.back().result.triangles);
    pending_graph = {};
}

auto triangulation_cache_state::release() noexcept -> void {
    pending_graph = {};
    release_vector(entries);
    total_units = 0U;
    tick = 0U;
}

}  // namespace clipper2next::internal
