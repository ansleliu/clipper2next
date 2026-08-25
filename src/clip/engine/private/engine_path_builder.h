#pragma once

#include "clip/engine/private/engine_types.h"
#include "clipper2next/geotypes/point.hpp"

#include <span>

namespace clipper2next::internal {

[[nodiscard]] auto is_very_small_triangle(const output_point_node& output_point) noexcept -> bool;
[[nodiscard]] auto path64_point_count(output_point_node* output_point,
                                      bool reverse,
                                      bool is_open) noexcept -> std::size_t;
[[nodiscard]] auto build_path64(output_point_node* output_point,
                                bool reverse,
                                bool is_open,
                                Path64& path) -> bool;
[[nodiscard]] auto build_path64_into(output_point_node* output_point,
                                     bool reverse,
                                     bool is_open,
                                     std::span<geotypes::Point2i64> path) -> bool;
[[nodiscard]] auto build_pathd(output_point_node* output_point,
                               bool reverse,
                               bool is_open,
                               PathD& path,
                               double inverse_scale) -> bool;

}  // namespace clipper2next::internal
