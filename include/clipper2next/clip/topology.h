// SPDX-License-Identifier: BSL-1.0

#pragma once

#include "clipper2next/api/export.h"
#include "clipper2next/api/options.h"
#include "clipper2next/clip/borrowed_paths.h"
#include "clipper2next/clip/topology_writer.h"
#include "clipper2next/clip/types.h"

#include <cstddef>
#include <limits>

namespace clipper2next {

struct borrowed_clip_limits64 final {
    std::size_t maximum_input_path_count{(std::numeric_limits<std::size_t>::max)()};
    std::size_t maximum_input_point_count{(std::numeric_limits<std::size_t>::max)()};
    std::size_t maximum_output_polygon_count{(std::numeric_limits<std::size_t>::max)()};
    std::size_t maximum_output_ring_count{(std::numeric_limits<std::size_t>::max)()};
    std::size_t maximum_output_point_count{(std::numeric_limits<std::size_t>::max)()};
    // Bounds logical staging bytes, excluding engine storage and retained capacity.
    std::size_t maximum_staging_workspace_bytes{(std::numeric_limits<std::size_t>::max)()};
    std::size_t maximum_engine_work{(std::numeric_limits<std::size_t>::max)()};
    // Bounds logical active engine storage; retained thread-cache capacity is excluded.
    std::size_t maximum_engine_workspace_bytes{(std::numeric_limits<std::size_t>::max)()};
};

struct borrowed_clip_request64 final {
    ClipType clip_type{ClipType::NoClip};
    FillRule fill_rule{FillRule::EvenOdd};
    borrowed_paths64 subjects{};
    borrowed_paths64 clips{};
    execution_options options{.preserve_collinear = true};
    borrowed_clip_limits64 limits{};
};

struct topology_write_stats64 final {
    std::size_t input_path_count{};
    std::size_t input_point_count{};
    std::size_t input_collection_point_writes{};
    std::size_t engine_input_point_writes{};
    std::size_t output_ring_acquire_count{};
    std::size_t output_final_point_writes{};
    std::size_t output_polygon_count{};
    std::size_t output_ring_count{};
    std::size_t output_point_count{};
    std::size_t staging_reallocation_count{};
    std::size_t peak_workspace_bytes{};
    std::size_t planned_engine_work{};
    std::size_t planned_engine_workspace_bytes{};
};

[[nodiscard]] CLIPPER2NEXT_API auto clip_topology_checked(
    const borrowed_clip_request64& request,
    topology_writer64 writer) -> clipper_result<topology_write_stats64>;

}  // namespace clipper2next
