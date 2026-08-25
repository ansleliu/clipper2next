#include "triangulation/private/triangulation_cache.h"

#include "support/private/checked_size.h"

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace clipper2next::internal {
namespace {

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
};

inline constexpr std::size_t minimum_cached_point_count = 512U;
inline constexpr std::size_t entry_capacity = 256U;
inline constexpr std::size_t maximum_entry_units = 64U * 1024U;
inline constexpr std::size_t total_unit_budget = 512U * 1024U;

[[nodiscard]] auto cache() -> triangulation_cache_state& {
    thread_local triangulation_cache_state state;
    return state;
}

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
    std::uint64_t seed = request.use_delaunay ? 0x0123456789abcdefULL : 0xfedcba9876543210ULL;
    hash_mix(seed, static_cast<std::uint64_t>(request.paths.size()));
    for (const auto& path : request.paths) {
        hash_mix(seed, static_cast<std::uint64_t>(path.size()));
        for (const auto& point : path) {
            hash_mix(seed, static_cast<std::uint64_t>(point.x));
            hash_mix(seed, static_cast<std::uint64_t>(point.y));
        }
    }
    return seed;
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

}  // namespace

auto try_get_cached_triangulation(const triangulation_request64& request,
                                  triangulation_result64& result) -> bool {
    const auto request_point_count = count_points(request.paths);
    if (!request.use_delaunay || request_point_count < minimum_cached_point_count ||
        checked_size_add(request.paths.size(), request_point_count) > maximum_entry_units) {
        return false;
    }

    auto& state = cache();
    const auto request_hash = hash_request(request);
    for (auto& entry : state.entries) {
        if (entry.hash != request_hash || entry.path_count != request.paths.size() ||
            entry.point_count != request_point_count ||
            entry.use_delaunay != request.use_delaunay ||
            !paths_exact_equal(entry.request_paths, request.paths)) {
            continue;
        }
        entry.last_used_tick = ++state.tick;
        result = entry.result;
        return true;
    }
    return false;
}

auto store_cached_triangulation(const triangulation_request64& request,
                                const triangulation_result64& result) -> void {
    const auto request_point_count = count_points(request.paths);
    if (!request.use_delaunay || request_point_count < minimum_cached_point_count) { return; }

    const auto units = checked_size_add(
        count_cache_units(request.paths), count_cache_units(result.triangles));
    if (units > maximum_entry_units) { return; }

    auto& state = cache();
    while (!state.entries.empty() &&
           (state.entries.size() >= entry_capacity ||
            units > total_unit_budget - state.total_units)) {
        evict_least_recently_used(state);
    }

    state.entries.push_back({.hash = hash_request(request),
                             .path_count = request.paths.size(),
                             .point_count = request_point_count,
                             .cached_units = units,
                             .use_delaunay = request.use_delaunay,
                             .request_paths = request.paths,
                             .result = result,
                             .last_used_tick = ++state.tick});
    state.total_units = checked_size_add(state.total_units, units);
}

auto release_triangulation_cache() noexcept -> void {
    auto& state = cache();
    std::vector<triangulation_cache_entry>{}.swap(state.entries);
    state.total_units = 0U;
}

}  // namespace clipper2next::internal
