#include "clip/engine/private/engine_intersections.h"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace clipper2next::internal {

auto build_intersection_list_from_contiguous_unit_runs(
    std::vector<active_edge_node*>& edges,
    std::vector<active_edge_node*>& scratch,
    IntersectNodeList& intersections,
    int64_t top_y,
    int64_t bottom_y,
    predicate_policy policy) -> bool {
    const auto edge_count = edges.size();
    if (edge_count < 2U) { return false; }

    scratch.resize(edge_count);
    auto* input = &edges;
    auto* output = &scratch;
    const auto initial_intersection_count = intersections.size();

    bool first_pass_is_ordered = true;
    const auto paired_edge_count = edge_count - (edge_count % 2U);
    for (std::size_t index = 0U; index < paired_edge_count; index += 2U) {
        auto* first = edges[index];
        auto* second = edges[index + 1U];
        if (second->current_x < first->current_x) {
            add_intersection_node(intersections, *first, *second, top_y, bottom_y, policy);
            scratch[index] = second;
            scratch[index + 1U] = first;
        } else {
            scratch[index] = first;
            scratch[index + 1U] = second;
        }
        if (index != 0U && scratch[index - 1U]->current_x > scratch[index]->current_x) {
            first_pass_is_ordered = false;
        }
    }
    if (paired_edge_count != edge_count) {
        scratch[paired_edge_count] = edges[paired_edge_count];
        if (scratch[paired_edge_count - 1U]->current_x >
            scratch[paired_edge_count]->current_x) {
            first_pass_is_ordered = false;
        }
    }

    std::swap(input, output);
    if (first_pass_is_ordered) {
        edges.swap(*input);
        return intersections.size() != initial_intersection_count;
    }

    for (std::size_t run_width = 2U; run_width < edge_count;) {
        bool output_runs_are_ordered = true;
        std::size_t output_index = 0U;
        for (std::size_t run_begin = 0U; run_begin < edge_count; run_begin += 2U * run_width) {
            const auto left_end = std::min(run_begin + run_width, edge_count);
            const auto right_end = std::min(left_end + run_width, edge_count);
            const auto pair_size = right_end - run_begin;

            if (left_end == right_end ||
                (*input)[left_end - 1U]->current_x <= (*input)[left_end]->current_x) {
                std::copy_n(input->begin() + static_cast<std::ptrdiff_t>(run_begin),
                            pair_size,
                            output->begin() + static_cast<std::ptrdiff_t>(output_index));
                output_index += pair_size;
            } else {
                auto left = run_begin;
                auto right = left_end;
                while (left < left_end && right < right_end) {
                    if ((*input)[right]->current_x < (*input)[left]->current_x) {
                        for (auto crossed = left_end; crossed != left; --crossed) {
                            add_intersection_node(intersections,
                                                  *(*input)[crossed - 1U],
                                                  *(*input)[right],
                                                  top_y,
                                                  bottom_y,
                                                  policy);
                        }
                        (*output)[output_index++] = (*input)[right++];
                    } else {
                        (*output)[output_index++] = (*input)[left++];
                    }
                }
                const auto left_remaining = left_end - left;
                std::copy_n(input->begin() + static_cast<std::ptrdiff_t>(left),
                            left_remaining,
                            output->begin() + static_cast<std::ptrdiff_t>(output_index));
                output_index += left_remaining;
                const auto right_remaining = right_end - right;
                std::copy_n(input->begin() + static_cast<std::ptrdiff_t>(right),
                            right_remaining,
                            output->begin() + static_cast<std::ptrdiff_t>(output_index));
                output_index += right_remaining;
            }

            if (run_begin != 0U &&
                (*output)[run_begin - 1U]->current_x > (*output)[run_begin]->current_x) {
                output_runs_are_ordered = false;
            }
        }

        std::swap(input, output);
        if (output_runs_are_ordered || run_width > edge_count / 2U) { break; }
        run_width *= 2U;
    }

    if (input != &edges) { edges.swap(*input); }
    return intersections.size() != initial_intersection_count;
}

}  // namespace clipper2next::internal
