#pragma once

#include <vector>

#include "offset/private/offset_group.h"
#include "offset/private/offset_state.h"
#include "clipper2next/offset/types.h"
#include "clipper2next/api/execution.h"
#include "clipper2next/polygon/poly_tree.h"
#include "clipper2next/core/path_set.h"
#include "support/private/engine_resource_plan.h"

namespace clipper2next::internal {

struct offset_algorithm_options final {
    double miter_limit{2.0};
    double arc_tolerance{0.0};
    std::size_t arc_segments_per_quadrant{};
    bool preserve_collinear{false};
    bool reverse_solution{false};
    bool check_input_coordinate_range{false};
    geotypes::CoordinateRounding coordinate_rounding{
        geotypes::CoordinateRounding::NearestEven};
};

auto execute_offset_algorithm(offset_state& state,
                              const std::vector<offset_group>& groups,
                              double delta,
                              Paths64& solution,
                              PolyTree64* solution_tree,
                              const offset_algorithm_options& options,
                              delta_callback_ref delta_callback,
                              sync_bulk_executor_ref executor = {}) -> void;

auto execute_offset_algorithm(offset_state& state,
                              const std::vector<offset_group>& groups,
                              double delta,
                              path_set64& solution,
                              const offset_algorithm_options& options,
                              delta_callback_ref delta_callback,
                              sync_bulk_executor_ref executor = {},
                              offset_engine_resource_context* resources = nullptr,
                              bool* output_is_disjoint_simple_shells = nullptr) -> void;

auto execute_offset_algorithm_scalar_reference(offset_state& state,
                                               const std::vector<offset_group>& groups,
                                               double delta,
                                               Paths64& solution,
                                               PolyTree64* solution_tree,
                                               const offset_algorithm_options& options,
                                               delta_callback_ref delta_callback) -> void;

}  // namespace clipper2next::internal
