#include "clip/engine/private/engine_intersection_cases.h"

#include "clip/engine/private/engine_output_topology.h"
#include "clip/engine/private/engine_topology.h"
#include "clip/engine/private/engine_winding.h"

#include <cstdlib>

namespace clipper2next::internal {
namespace {

[[nodiscard]] auto is_joined(const active_edge_node& edge) noexcept -> bool {
    return edge.join_with != JoinWith::NoJoin;
}

[[nodiscard]] auto fill_adjusted_wind_count(const clipper_base_state& state, int wind_count)
    -> int {
    switch (state.fillrule_) {
    case FillRule::EvenOdd:
    case FillRule::NonZero: {
        return std::abs(wind_count);
    }
    default: {
        return state.fillrule_ == state.fillpos ? wind_count : -wind_count;
    }
    }
}

[[nodiscard]] auto fill_adjusted_secondary_wind_count(const clipper_base_state& state,
                                                      int64_t wind_count) -> int64_t {
    switch (state.fillrule_) {
    case FillRule::EvenOdd:
    case FillRule::NonZero: {
        return std::abs(wind_count);
    }
    default: {
        return state.fillrule_ == state.fillpos ? wind_count : -wind_count;
    }
    }
}

[[nodiscard]] auto starts_local_minimum_for_clip_type(const clipper_base_state& state,
                                                      const active_edge_node& first,
                                                      int64_t first_wind_count2,
                                                      int64_t second_wind_count2) -> bool {
    switch (state.cliptype_) {
    case ClipType::Union: {
        return first_wind_count2 <= 0 && second_wind_count2 <= 0;
    }
    case ClipType::Difference: {
        return ((get_poly_type(first) == PathType::Clip) && (first_wind_count2 > 0) &&
                (second_wind_count2 > 0)) ||
               ((get_poly_type(first) == PathType::Subject) && (first_wind_count2 <= 0) &&
                (second_wind_count2 <= 0));
    }
    case ClipType::Xor: {
        return true;
    }
    default: {
        return first_wind_count2 > 0 && second_wind_count2 > 0;
    }
    }
}

}  // namespace

auto intersect_closed_edges(clipper_base_state& state,
                            bool& succeeded,
                            active_edge_node& first,
                            active_edge_node& second,
                            const Point64& point) -> void {
    auto add_local_min_poly = [&state](active_edge_node& local_first,
                                       active_edge_node& local_second,
                                       const Point64& local_point,
                                       bool is_new = false) {
        return add_local_min_polygon(state, local_first, local_second, local_point, is_new);
    };
    auto add_local_max_poly = [&state, &succeeded](active_edge_node& local_first,
                                                   active_edge_node& local_second,
                                                   const Point64& local_point) {
        auto result =
            add_local_max_polygon(state,
                                  local_first,
                                  local_second,
                                  local_point,
                                  [&state](active_edge_node& joined, const Point64& split_point) {
                                      split_joined_edge(state, joined, split_point);
                                  });
        if (!result.succeeded) { succeeded = false; }
        return result.output_point ? &result.output_point->get() : nullptr;
    };

    if (is_joined(first)) { split_joined_edge(state, first, point); }
    if (is_joined(second)) { split_joined_edge(state, second, point); }

    int old_first_wind_count = 0;
    int old_second_wind_count = 0;
    if (first.local_min->polytype == second.local_min->polytype) {
        if (state.fillrule_ == FillRule::EvenOdd) {
            old_first_wind_count = first.winding_count;
            first.winding_count = second.winding_count;
            second.winding_count = old_first_wind_count;
        } else {
            if (first.winding_count + second.wind_dx == 0) {
                first.winding_count = -first.winding_count;
            } else {
                first.winding_count += second.wind_dx;
            }

            if (second.winding_count - first.wind_dx == 0) {
                second.winding_count = -second.winding_count;
            } else {
                second.winding_count -= first.wind_dx;
            }
        }
    } else if (state.fillrule_ != FillRule::EvenOdd) {
        first.wind_cnt2 += second.wind_dx;
        second.wind_cnt2 -= first.wind_dx;
    } else {
        first.wind_cnt2 = (first.wind_cnt2 == 0 ? 1 : 0);
        second.wind_cnt2 = (second.wind_cnt2 == 0 ? 1 : 0);
    }

    old_first_wind_count = fill_adjusted_wind_count(state, first.winding_count);
    old_second_wind_count = fill_adjusted_wind_count(state, second.winding_count);

    const bool first_wind_count_in_01 = old_first_wind_count == 0 || old_first_wind_count == 1;
    const bool second_wind_count_in_01 = old_second_wind_count == 0 || old_second_wind_count == 1;

    if ((!is_hot_edge(first) && !first_wind_count_in_01) ||
        (!is_hot_edge(second) && !second_wind_count_in_01)) {
        return;
    }

    if (is_hot_edge(first) && is_hot_edge(second)) {
        if ((old_first_wind_count != 0 && old_first_wind_count != 1) ||
            (old_second_wind_count != 0 && old_second_wind_count != 1) ||
            (first.local_min->polytype != second.local_min->polytype &&
             state.cliptype_ != ClipType::Xor)) {
            static_cast<void>(add_local_max_poly(first, second, point));
        } else if (is_front(first) || (first.outrec == second.outrec)) {
            static_cast<void>(add_local_max_poly(first, second, point));
            static_cast<void>(add_local_min_poly(first, second, point));
        } else {
            static_cast<void>(add_output_point(first, point));
            static_cast<void>(add_output_point(second, point));
            swap_outrecs(first, second);
        }
    } else if (is_hot_edge(first)) {
        static_cast<void>(add_output_point(first, point));
        swap_outrecs(first, second);
    } else if (is_hot_edge(second)) {
        static_cast<void>(add_output_point(second, point));
        swap_outrecs(first, second);
    } else {
        const auto first_wind_count2 = fill_adjusted_secondary_wind_count(state, first.wind_cnt2);
        const auto second_wind_count2 = fill_adjusted_secondary_wind_count(state, second.wind_cnt2);

        if (!is_same_poly_type(first, second)) {
            static_cast<void>(add_local_min_poly(first, second, point, false));
        } else if (old_first_wind_count == 1 && old_second_wind_count == 1) {
            if (starts_local_minimum_for_clip_type(
                    state, first, first_wind_count2, second_wind_count2)) {
                static_cast<void>(add_local_min_poly(first, second, point, false));
            }
        }
    }
}

}  // namespace clipper2next::internal
