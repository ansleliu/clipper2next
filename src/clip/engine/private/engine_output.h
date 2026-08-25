#pragma once

#include "clip/engine/private/engine_types.h"

namespace clipper2next::internal {

[[nodiscard]] auto point_count(output_point_node* output_point) -> int;
[[nodiscard]] auto duplicate_out_point(output_point_node* output_point, bool insert_after)
    -> output_point_node*;
[[nodiscard]] auto dispose_out_point(output_point_node* output_point) -> output_point_node*;
auto dispose_out_points(output_record_node* output_record) -> void;

}  // namespace clipper2next::internal
