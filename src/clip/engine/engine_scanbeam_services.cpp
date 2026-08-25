#include "clip/engine/private/engine_scanbeam_services.h"

#include "clip/engine/private/engine_ael_builder.h"
#include "clip/engine/private/engine_active_list.h"
#include "clip/engine/private/engine_geometry.h"
#include "clip/engine/private/engine_horizontal.h"
#include "clip/engine/private/engine_intersections.h"
#include "clip/engine/private/engine_lifecycle.h"
#include "clip/engine/private/engine_output_topology.h"
#include "clip/engine/private/engine_scanbeam_schedule.h"

#include <algorithm>
#include <cassert>
#include <utility>

namespace clipper2next::internal {
namespace {

[[nodiscard]] auto has_order_dependent_coincident_intersections(
    const IntersectNodeList& intersections) -> bool {
    for (auto first = intersections.begin(); first != intersections.end();) {
        auto last = first + 1;
        while (last != intersections.end() && last->pt.x == first->pt.x &&
               last->pt.y == first->pt.y) {
            ++last;
        }
        for (auto current = first; current != last; ++current) {
            for (auto other = current + 1; other != last; ++other) {
                if (current->edge1 == other->edge1 || current->edge1 == other->edge2 ||
                    current->edge2 == other->edge1 || current->edge2 == other->edge2) {
                    return true;
                }
            }
        }
        first = last;
    }
    return false;
}

}  // namespace

engine_scanbeam_services::engine_scanbeam_services(
    clipper_base_state& state,
    engine_scanbeam_orchestration_options options,
    bool& succeeded,
    engine_intersection_services& intersection_services) noexcept
    : state_(&state),
      options_(options),
      succeeded_(&succeeded),
      intersection_services_(&intersection_services),
      top_edges_at_current_y_(state.scanbeam_top_edges_),
      contiguous_unit_edges_(state.scanbeam_unit_edges_),
      contiguous_unit_scratch_(state.scanbeam_unit_scratch_) {}

auto engine_scanbeam_services::reset() -> void {
    current_x_adjusted_top_y_.reset();
    if (top_edges_at_current_y_.capacity() < state_->minima_list_.size()) {
        top_edges_at_current_y_.reserve(state_->minima_list_.size());
    }
    top_edges_at_current_y_.clear();
    reset_engine_state(*state_);
}

auto engine_scanbeam_services::pop_scanline(int64_t& y) -> bool {
    return internal::pop_scanline(state_->scanline_list_, y);
}

auto engine_scanbeam_services::insert_local_minima_into_ael(int64_t bot_y) -> void {
    internal::insert_local_minima_into_ael(
        *state_,
        bot_y,
        [this](active_edge_node& left, active_edge_node& right, const Point64& point, bool is_new) {
            return add_local_min_polygon(*state_, left, right, point, is_new);
        },
        [this](active_edge_node& edge, const Point64& point, bool check_curr_x) {
            check_join_left(*state_, *succeeded_, edge, point, check_curr_x);
        },
        [this](active_edge_node& edge, const Point64& point, bool check_curr_x) {
            check_join_right(*state_, *succeeded_, edge, point, check_curr_x);
        },
        [this](active_edge_node& first, active_edge_node& second, const Point64& point) {
            intersect_edges(*state_,
                            options_.has_open_paths,
                            *succeeded_,
                            first,
                            second,
                            point);
        });
}

auto engine_scanbeam_services::pop_horz(active_edge_node*& edge) -> bool {
    return pop_horizontal(state_->sel_, edge);
}

auto engine_scanbeam_services::do_horizontal(active_edge_node& edge) -> void {
    internal::do_horizontal(*state_, edge, options_, *succeeded_);
}

auto engine_scanbeam_services::convert_horz_segments_to_joins() -> void {
    convert_horizontal_segments_to_joins(state_->horz_seg_list_, state_->horz_join_list_);
}

auto engine_scanbeam_services::build_sorted_intersection_nodes(
    int64_t top_y,
    bool use_contiguous_unit_runs) -> bool {
    const auto has_intersections =
        use_contiguous_unit_runs
            ? build_intersection_list_from_contiguous_unit_runs(
                  contiguous_unit_edges_,
                  contiguous_unit_scratch_,
                  state_->intersect_nodes_,
                  top_y,
                  state_->bot_y_,
                  intersection_services_->intersection_policy)
            : build_intersection_list_from_sel(
                  state_->sel_,
                  state_->intersect_nodes_,
                  top_y,
                  state_->bot_y_,
                  intersection_services_->intersection_policy);
    if (!has_intersections) { return false; }

    sort_intersections(state_->intersect_nodes_);
    if (options_.schedule_mode != scanbeam_schedule_mode::monotone_runs ||
        !has_order_dependent_coincident_intersections(state_->intersect_nodes_)) {
        return true;
    }

    // Coalesced runs produce the same intersection set as unit runs, but
    // equal-coordinate nodes retain generation-order tie breaks. Rebuild
    // only those ambiguous scanbeams in legacy order.
    state_->intersect_nodes_.clear();
    const auto fallback_schedule = prepare_contiguous_unit_scanbeam_schedule(
        *state_->actives_, top_y, contiguous_unit_edges_, top_edges_at_current_y_);
    top_edges_at_current_y_.clear();
    if (!fallback_schedule.has_inversion ||
        !build_intersection_list_from_contiguous_unit_runs(
            contiguous_unit_edges_,
            contiguous_unit_scratch_,
            state_->intersect_nodes_,
            top_y,
            state_->bot_y_,
            intersection_services_->intersection_policy)) {
        return false;
    }
    sort_intersections(state_->intersect_nodes_);
    return true;
}

auto engine_scanbeam_services::do_intersections(int64_t top_y) -> void {
    if (!state_->actives_ || !state_->actives_->next_in_ael) {
        top_edges_at_current_y_.clear();
        return;
    }
    const auto use_contiguous_unit_runs =
        options_.schedule_mode == scanbeam_schedule_mode::unit_runs;
    const auto schedule =
        use_contiguous_unit_runs
            ? prepare_contiguous_unit_scanbeam_schedule(*state_->actives_,
                                                        top_y,
                                                        contiguous_unit_edges_,
                                                        top_edges_at_current_y_)
            : prepare_global_scanbeam_schedule(*state_->actives_,
                                               top_y,
                                               state_->sel_,
                                               top_edges_at_current_y_,
                                               options_.schedule_mode);
    if (!schedule.has_inversion) {
        current_x_adjusted_top_y_ = top_y;
        return;
    }
    current_x_adjusted_top_y_.reset();
    if (!use_contiguous_unit_runs) { top_edges_at_current_y_.clear(); }
    if (state_->intersect_nodes_.capacity() < schedule.active_edge_count) {
        state_->intersect_nodes_.reserve(schedule.active_edge_count);
    }

    if (!build_sorted_intersection_nodes(top_y, use_contiguous_unit_runs)) { return; }

    IntersectNodeList::iterator node_iter, node_iter2;
    for (node_iter = state_->intersect_nodes_.begin(); node_iter != state_->intersect_nodes_.end();
         ++node_iter) {
        if (!intersection_edges_are_adjacent_in_ael(*node_iter)) {
            node_iter2 =
                find_next_adjacent_intersection(node_iter + 1, state_->intersect_nodes_.end());
            if (node_iter2 == state_->intersect_nodes_.end()) {
                assert(false && "scanbeam intersection invariant broken: no adjacent pair found");
                *succeeded_ = false;
                state_->intersect_nodes_.clear();
                return;
            }
            std::swap(*node_iter, *node_iter2);
        }

        IntersectNode& node = *node_iter;
        active_edge_node& first_edge = node.first_edge();
        active_edge_node& second_edge = node.second_edge();
        intersect_edges(*state_,
                        options_.has_open_paths,
                        *succeeded_,
                        first_edge,
                        second_edge,
                        node.pt);
        swap_positions_in_ael(first_edge, second_edge, state_->actives_);

        first_edge.current_x = node.pt.x;
        second_edge.current_x = node.pt.x;
        check_join_left(*state_, *succeeded_, second_edge, node.pt, true);
        check_join_right(*state_, *succeeded_, first_edge, node.pt, true);
    }
    state_->intersect_nodes_.clear();

    if (use_contiguous_unit_runs) {
        top_edges_at_current_y_.clear();
        for (auto* edge : contiguous_unit_edges_) {
            edge->current_x = edge->scanbeam_top_x;
            if (edge->top_point.y == top_y) { top_edges_at_current_y_.push_back(edge); }
        }
        current_x_adjusted_top_y_ = top_y;
    }
}

auto engine_scanbeam_services::do_top_of_scanbeam(int64_t top_y) -> void {
    const bool current_x_already_at_top =
        current_x_adjusted_top_y_.has_value() && *current_x_adjusted_top_y_ == top_y;
    if (current_x_already_at_top) {
        internal::do_known_top_edges_of_scanbeam(
            *state_, top_y, top_edges_at_current_y_, options_, *succeeded_);
    } else {
        internal::do_top_of_scanbeam(*state_, top_y, options_, *succeeded_, false);
    }
    current_x_adjusted_top_y_.reset();
    top_edges_at_current_y_.clear();
}

auto engine_scanbeam_services::process_horz_joins() -> void {
    process_horizontal_joins(
        state_->horz_join_list_, state_->output_owner_, state_->using_polytree_);
}

auto engine_scanbeam_services::succeeded() const -> bool {
    return *succeeded_;
}

}  // namespace clipper2next::internal
