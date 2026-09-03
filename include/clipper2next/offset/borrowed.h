// SPDX-License-Identifier: BSL-1.0

#pragma once

#include "clipper2next/api/options.h"
#include "clipper2next/api/error.h"
#include "clipper2next/clip/topology.h"
#include "clipper2next/core/path_set.h"
#include "clipper2next/offset/types.h"

#include <cstddef>
#include <cstdint>
#include <limits>

namespace clipper2next {

struct borrowed_offset_limits64 final {
    std::size_t maximum_input_path_count{(std::numeric_limits<std::size_t>::max)()};
    std::size_t maximum_input_point_count{(std::numeric_limits<std::size_t>::max)()};
    std::size_t maximum_output_path_count{(std::numeric_limits<std::size_t>::max)()};
    std::size_t maximum_output_point_count{(std::numeric_limits<std::size_t>::max)()};
    // Bounds logical simultaneously-active input and output stage bytes. Retained
    // offset-kernel cache capacity is intentionally outside this limit.
    std::size_t maximum_staging_workspace_bytes{(std::numeric_limits<std::size_t>::max)()};
    std::size_t maximum_engine_work{(std::numeric_limits<std::size_t>::max)()};
    // Bounds logical active offset/cleanup storage; retained thread-cache capacity is excluded.
    std::size_t maximum_engine_workspace_bytes{(std::numeric_limits<std::size_t>::max)()};
};

struct borrowed_offset_request64 final {
    borrowed_paths64 paths{};
    double delta{0.0};
    JoinType join_type{JoinType::Miter};
    EndType end_type{EndType::Polygon};
    double miter_limit{2.0};
    // Maximum radial approximation error in coordinate units. Zero selects
    // Clipper2's delta-relative default tolerance.
    double arc_tolerance{0.0};
    // Nonzero selects an exact number of round-join segments per quadrant
    // and takes precedence over arc_tolerance.
    std::size_t arc_segments_per_quadrant{};
    geotypes::CoordinateRounding coordinate_rounding{
        geotypes::CoordinateRounding::NearestEven};
    execution_options options{};
    borrowed_offset_limits64 limits{};
};

struct borrowed_offset_stage_stats64 final {
    std::size_t input_path_count{};
    std::size_t input_point_count{};
    std::size_t input_collection_point_writes{};
    std::size_t engine_input_point_writes{};
    std::size_t output_path_count{};
    std::size_t output_point_count{};
    std::size_t staging_reallocation_count{};
    std::size_t peak_workspace_bytes{};
    std::size_t planned_engine_work{};
    std::size_t planned_engine_workspace_bytes{};
    // True only when the stage proved that every output path is a simple,
    // pairwise-disjoint shell. Consumers may then materialize one polygon per
    // path without running another boolean union.
    bool output_is_disjoint_simple_shells{};
};

struct borrowed_offset_stage_result64 final {
    path_set64 paths;
    borrowed_offset_stage_stats64 stats{};
};

using expected_borrowed_offset_stage_result64 =
    clipper_result<borrowed_offset_stage_result64>;

}  // namespace clipper2next
