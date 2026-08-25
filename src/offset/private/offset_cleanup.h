#pragma once

#include "clip/private/boolean_union_service.h"

#include <utility>

namespace clipper2next::internal {

inline auto cleanup_offset_solution(Paths64& solution,
                                    PolyTree64* solution_tree,
                                    bool preserve_collinear,
                                    bool reverse_solution,
                                    bool paths_reversed) -> void {
    if (solution.empty()) { return; }

    clip_union_options union_options;
    union_options.fill_rule = paths_reversed ? FillRule::Negative : FillRule::Positive;
    union_options.options.preserve_collinear = preserve_collinear;
    union_options.options.reverse_solution = reverse_solution != paths_reversed;
    union_options.decompose_disjoint_components = false;

    if (solution_tree) {
        union_closed_paths_into_tree(solution, *solution_tree, union_options);
    } else {
        solution = union_closed_paths(std::move(solution), union_options);
    }
}

}  // namespace clipper2next::internal
