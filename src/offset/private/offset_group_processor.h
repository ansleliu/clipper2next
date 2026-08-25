#pragma once

#include "offset/private/offset_group.h"
#include "offset/private/offset_state.h"
#include "clipper2next/offset/types.h"
#include "clipper2next/api/execution.h"
#include "clipper2next/core/path_set.h"

#include <span>

namespace clipper2next::internal {

inline constexpr auto offset_parallel_minimum_path_count =
    std::size_t{512U};
inline constexpr auto offset_parallel_minimum_point_count =
    std::size_t{524'288U};
inline constexpr auto offset_parallel_maximum_concurrency =
    std::size_t{16U};
inline constexpr auto offset_parallel_minimum_concurrency =
    std::size_t{16U};

struct offset_group_execution_options final {
    double miter_limit{2.0};
    double arc_tolerance{0.0};
    std::size_t arc_segments_per_quadrant{};
    bool reverse_solution{false};
    geotypes::CoordinateRounding coordinate_rounding{
        geotypes::CoordinateRounding::NearestEven};
};

struct offset_path_build_result final {
    Paths64 paths;
};

struct offset_path_work_profile final {
    std::size_t input_index{};
    std::size_t point_count{};
};

auto append_offset_path(
    offset_state& state,
    const offset_group& group,
    std::span<const Point64> path,
    const offset_group_execution_options& options,
    path_set64& output) -> void;

auto prepare_offset_group_state(offset_state& state,
                                const offset_group& group,
                                const offset_group_execution_options& options) -> void;

auto build_offset_path_result(offset_state& state,
                              const offset_group& group,
                              std::span<const Point64> path,
                              const offset_group_execution_options& options,
                              delta_callback_ref delta_callback) -> offset_path_build_result;

auto build_offset_group_paths(offset_state& state,
                              const offset_group& group,
                              const offset_group_execution_options& options,
                              delta_callback_ref delta_callback,
                              Paths64& output) -> void;

auto build_offset_group_paths(offset_state& state,
                              const offset_group& group,
                              const offset_group_execution_options& options,
                              delta_callback_ref delta_callback,
                              path_set64& output) -> void;

[[nodiscard]] auto is_offset_group_parallel_eligible(const offset_group& group) -> bool;

auto build_offset_group_paths_parallel(offset_state& state,
                                       const offset_group& group,
                                       const offset_group_execution_options& options,
                                       double delta,
                                       sync_bulk_executor_ref executor,
                                       Paths64& output) -> void;

auto build_offset_group_paths_parallel(offset_state& state,
                                       const offset_group& group,
                                       const offset_group_execution_options& options,
                                       double delta,
                                       sync_bulk_executor_ref executor,
                                       path_set64& output) -> void;

}  // namespace clipper2next::internal
