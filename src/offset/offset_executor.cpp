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
                                           const offset_arc_parameters& arc) noexcept -> size_t {
    if (join_type == JoinType::Round) { return arc_step_count(arc, 2 * pi); }
    return 4;
}

auto estimate_path_output_capacity(size_t path_size,
                                   JoinType join_type,
                                   EndType end_type,
                                   const offset_arc_parameters& arc) noexcept -> size_t {
    if (path_size == 0) { return 0; }
    if (path_size == 1) { return estimate_single_point_output_capacity(join_type, arc); }

    const auto round_steps = arc_step_count(arc, pi);
    const auto polygon_capacity =
        join_type == JoinType::Round ? saturating_product(path_size, round_steps)
                                     : saturating_product(path_size, 2U);

    if (end_type == EndType::Polygon || end_type == EndType::Joined) { return polygon_capacity; }

    const auto path_with_caps = saturating_sum(path_size, 2U);
    const auto open_minimum = saturating_product(path_with_caps, 2U);
    if (join_type != JoinType::Round && end_type != EndType::Round) { return open_minimum; }
    return std::max(open_minimum, saturating_product(path_with_caps, round_steps));
}

}  // namespace clipper2next::internal
