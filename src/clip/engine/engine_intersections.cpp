#include "clip/engine/private/engine_intersections.h"

#include "clipper2next/core.h"
#include "clip/engine/private/engine_active_list.h"
#include "clip/engine/private/engine_geometry.h"
#include "geometry/private/geometry_predicates.h"

#include <algorithm>
#include <cmath>

namespace clipper2next::internal {

namespace {

auto move_right_edge_before_left(active_edge_node*& sorted_edges,
                                 active_edge_node* left,
                                 active_edge_node*& right,
                                 active_edge_node*& left_end,
                                 active_edge_node*& current_base,
                                 active_edge_node* previous_base,
                                  active_edge_node* right_end,
                                  IntersectNodeList& intersections,
                                  int64_t top_y,
                                  int64_t bottom_y,
                                  predicate_policy policy) -> void {
    auto* temp = right->prev_in_sel.get();
    for (;;) {
        add_intersection_node(intersections, *temp, *right, top_y, bottom_y, policy);
        if (temp == left) { break; }
        temp = temp->prev_in_sel.get();
    }

    temp = right;
    right = extract_from_sel(temp);
    left_end = right;
    insert_before_in_sel(temp, left);
    if (left != current_base) { return; }

    current_base = temp;
    current_base->jump = right_end;
    if (!previous_base) {
        sorted_edges = current_base;
    } else {
        previous_base->jump = current_base;
    }
}

}  // namespace

auto add_intersection_node(IntersectNodeList& intersections,
                           active_edge_node& first,
                           active_edge_node& second,
                           int64_t top_y,
                           int64_t bottom_y,
                           predicate_policy policy) -> void {
    Point64 intersection;
    const auto has_intersection =
        policy.mode == precision_mode::fast
            ? line_intersection_point_in_clipper_range_fast(first.bottom,
                                                            first.top_point,
                                                            second.bottom,
                                                            second.top_point,
                                                            intersection)
            : line_intersection_point(first.bottom,
                                      first.top_point,
                                      second.bottom,
                                      second.top_point,
                                      intersection,
                                      policy);
    if (!has_intersection) {
        intersection = Point64(first.current_x, top_y);
    }

    // Rounding can place the calculated point just outside the scanbeam.
    if (intersection.y > bottom_y || intersection.y < top_y) {
        const double first_abs_dx = std::fabs(first.dx);
        const double second_abs_dx = std::fabs(second.dx);
        if (first_abs_dx > 100 && second_abs_dx > 100) {
            if (first_abs_dx > second_abs_dx) {
                intersection =
                    closest_point_on_segment(intersection, first.bottom, first.top_point);
            } else {
                intersection =
                    closest_point_on_segment(intersection, second.bottom, second.top_point);
            }
        } else if (first_abs_dx > 100) {
            intersection = closest_point_on_segment(intersection, first.bottom, first.top_point);
        } else if (second_abs_dx > 100) {
            intersection = closest_point_on_segment(intersection, second.bottom, second.top_point);
        } else {
            if (intersection.y < top_y) {
                intersection.y = top_y;
            } else {
                intersection.y = bottom_y;
            }
            if (first_abs_dx < second_abs_dx) {
                intersection.x = top_x(first, intersection.y);
            } else {
                intersection.x = top_x(second, intersection.y);
            }
        }
    }

    intersections.emplace_back(first, second, intersection);
}

auto build_intersection_list_from_sel(active_edge_node*& sorted_edges,
                                      IntersectNodeList& intersections,
                                      int64_t top_y,
                                      int64_t bottom_y,
                                      predicate_policy policy) -> bool {
    auto* left = sorted_edges;
    active_edge_node* right = nullptr;
    active_edge_node* left_end = nullptr;
    active_edge_node* right_end = nullptr;
    active_edge_node* current_base = nullptr;

    while (left && left->jump) {
        active_edge_node* previous_base = nullptr;
        while (left && left->jump) {
            current_base = left;
            right = left->jump;
            left_end = right;
            right_end = right->jump;
            left->jump = right_end;
            while (left != left_end && right != right_end) {
                if (right->current_x < left->current_x) {
                    move_right_edge_before_left(sorted_edges,
                                                left,
                                                right,
                                                left_end,
                                                current_base,
                                                previous_base,
                                                right_end,
                                                intersections,
                                                top_y,
                                                bottom_y,
                                                policy);
                } else {
                    left = left->next_in_sel;
                }
            }
            previous_base = current_base;
            left = right_end;
        }
        left = sorted_edges;
    }
    return !intersections.empty();
}

auto build_intersection_list_from_sel(active_edge_node_ref& sorted_edges,
                                      IntersectNodeList& intersections,
                                      int64_t top_y,
                                      int64_t bottom_y,
                                      predicate_policy policy) -> bool {
    auto* raw_sorted_edges = sorted_edges.get();
    const auto result =
        build_intersection_list_from_sel(raw_sorted_edges, intersections, top_y, bottom_y, policy);
    sorted_edges = raw_sorted_edges;
    return result;
}

auto compare_intersections_bottom_up(const IntersectNode& first, const IntersectNode& second)
    -> bool {
    return (first.pt.y == second.pt.y) ? (first.pt.x < second.pt.x) : (first.pt.y > second.pt.y);
}

auto intersection_edges_are_adjacent_in_ael(const IntersectNode& node) noexcept -> bool {
    const auto& first = node.first_edge();
    const auto& second = node.second_edge();
    return (first.next_in_ael == &second) || (first.prev_in_ael == &second);
}

auto find_next_adjacent_intersection(IntersectNodeList::iterator first,
                                     IntersectNodeList::iterator last)
    -> IntersectNodeList::iterator {
    return std::find_if(first, last, [](const IntersectNode& node) {
        return intersection_edges_are_adjacent_in_ael(node);
    });
}

auto sort_intersections(IntersectNodeList& intersections) -> void {
    std::sort(
        intersections.begin(), intersections.end(), internal::compare_intersections_bottom_up);
}

}  // namespace clipper2next::internal
