#include "api/private/borrowed_topology_pipeline.h"

#include "clip/engine/private/engine_lifecycle.h"
#include "clip/private/borrowed_topology_access.h"
#include "clip/private/clip_request_validation.h"
#include "support/private/checked_size.h"

#include <new>

namespace clipper2next::internal {
namespace {

[[nodiscard]] auto input_staging_exceeds(std::size_t path_count,
                                         std::size_t point_count,
                                         std::size_t maximum_bytes) noexcept -> bool {
    constexpr auto path_bytes = sizeof(path_source_contract::borrowed_path_measurement64);
    constexpr auto point_bytes = sizeof(Vertex);
    if (path_count > maximum_bytes / path_bytes) { return true; }
    const auto measurement_bytes = path_count * path_bytes;
    return point_count > (maximum_bytes - measurement_bytes) / point_bytes;
}

}  // namespace

auto measure_paths(const borrowed_paths64& source,
                   measured_paths64& result,
                   const borrowed_clip_limits64& limits,
                   std::size_t& total_path_count,
                   std::size_t& total_point_count,
                   std::size_t& reallocation_count) noexcept -> clipper_error_code {
    std::size_t path_count = 0U;
    if (const auto error = borrowed_paths64_access::path_count(source, path_count);
        error != clipper_error_code::ok) {
        return error;
    }
    if (exceeds(total_path_count, limits.maximum_input_path_count) ||
        path_count > limits.maximum_input_path_count - total_path_count) {
        return clipper_error_code::resource_limit;
    }
    total_path_count += path_count;
    if (input_staging_exceeds(
            total_path_count, total_point_count, limits.maximum_staging_workspace_bytes)) {
        return clipper_error_code::resource_limit;
    }

    try {
        result.paths.clear();
        result.source_point_count = 0U;
        result.normalized_point_count = 0U;
        resize_and_count_growth(result.paths, path_count, reallocation_count);
    } catch (const std::bad_alloc&) {
        return clipper_error_code::allocation_failure;
    } catch (...) {
        return clipper_error_code::resource_limit;
    }

    for (std::size_t index = 0; index < path_count; ++index) {
        auto& measurement = result.paths[index];
        if (const auto error = borrowed_paths64_access::measure_path(source, index, measurement);
            error != clipper_error_code::ok) {
            return error;
        }
        if (const auto error = checked_accumulate(
                result.source_point_count, measurement.source_point_count);
            error != clipper_error_code::ok) {
            return error;
        }
        if (exceeds(total_point_count, limits.maximum_input_point_count) ||
            measurement.source_point_count > limits.maximum_input_point_count - total_point_count) {
            return clipper_error_code::resource_limit;
        }
        total_point_count += measurement.source_point_count;
        if (input_staging_exceeds(
                total_path_count, total_point_count, limits.maximum_staging_workspace_bytes)) {
            return clipper_error_code::resource_limit;
        }
        if (const auto error = checked_accumulate(
                result.normalized_point_count, measurement.normalized_point_count);
            error != clipper_error_code::ok) {
            return error;
        }
    }
    return clipper_error_code::ok;
}

auto load_paths(clipper_base_state& state,
                const borrowed_paths64& source,
                const measured_paths64& measured,
                PathType path_type,
                topology_write_stats64& stats) -> clipper_error_code {
    std::size_t current_path_count = 0U;
    if (const auto error = borrowed_paths64_access::path_count(source, current_path_count);
        error != clipper_error_code::ok) {
        return error;
    }
    if (current_path_count != measured.paths.size()) { return clipper_error_code::input_changed; }
    if (measured.source_point_count == 0U) { return clipper_error_code::ok; }

    state.vertex_lists_.reserve(checked_size_add(state.vertex_lists_.size(), std::size_t{1}));
    state.minima_list_.reserve(checked_size_add(
        state.minima_list_.size(), checked_size_multiply(measured.paths.size(), std::size_t{2})));
    auto* next_vertex = state.vertex_lists_.acquire(measured.source_point_count);

    for (std::size_t path_index = 0; path_index < measured.paths.size(); ++path_index) {
        const auto& expected = measured.paths[path_index];
        std::size_t normalized_count = 0U;
        std::size_t point_write_count = 0U;
        const auto error = borrowed_paths64_access::copy_path(
            source,
            path_index,
            &next_vertex->pt,
            sizeof(Vertex),
            expected.source_point_count,
            expected.normalized_point_count,
            normalized_count,
            point_write_count);
        if (error != clipper_error_code::ok) { return error; }

        for (std::size_t index = 0; index < normalized_count; ++index) {
            const auto& point = next_vertex[index].pt;
            if (!clip_coordinate_in_range(point.x) || !clip_coordinate_in_range(point.y)) {
                return clipper_error_code::coordinate_range;
            }
        }
        stats.engine_input_point_writes =
            checked_size_add(stats.engine_input_point_writes, point_write_count);
        initialize_vertex_path(next_vertex, normalized_count, path_type, false, state.minima_list_);
        next_vertex += normalized_count;
    }
    state.minima_list_sorted_ = false;
    return clipper_error_code::ok;
}

}  // namespace clipper2next::internal
