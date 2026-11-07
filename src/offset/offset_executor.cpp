#include "offset/private/offset_executor.h"

#include <algorithm>
#include <limits>

namespace clipper2next::internal {
namespace {

[[nodiscard]] auto saturating_product(std::size_t left, std::size_t right) noexcept
    -> std::size_t {
    const auto maximum = (std::numeric_limits<std::size_t>::max)();
    if (left != 0U && right > maximum / left) { return maximum; }
    return left * right;
}

[[nodiscard]] auto saturating_sum(std::size_t left, std::size_t right) noexcept -> std::size_t {
    const auto maximum = (std::numeric_limits<std::size_t>::max)();
    if (right > maximum - left) { return maximum; }
    return left + right;
}

}  // namespace

auto estimate_single_point_output_capacity(JoinType join_type,
                                           const offset_arc_parameters& arc, double delta) noexcept -> size_t {
    if (join_type == JoinType::Round) { return offset_circle_steps(arc, delta); }
    return 4;
}

auto estimate_path_output_capacity(size_t path_size,
                                   JoinType join_type,
                                   EndType end_type,
                                   const offset_arc_parameters& arc, double delta) noexcept -> size_t {
    if (path_size == 0) { return 0; }
    if (path_size == 1) { return estimate_single_point_output_capacity(join_type, arc, delta); }

    const auto join_bound = offset_join_point_bound(join_type, arc);
    const auto polygon_capacity = saturating_product(path_size, join_bound);
    // Joined generates each orientation in the same path_out buffer; raw
    // storage holds both. This function reserves the one scratch contour.
    if (end_type == EndType::Polygon || end_type == EndType::Joined) { return polygon_capacity; }
    const auto cap_bound = end_type == EndType::Round
        ? saturating_sum(arc_step_count(arc, pi), 1U) : std::size_t{2U};
    return saturating_sum(saturating_product(path_size - 2U, saturating_product(join_bound, 2U)),
                          saturating_product(cap_bound, 2U));
}

}  // namespace clipper2next::internal
