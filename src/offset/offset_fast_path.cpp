#include "offset/private/offset_fast_path.h"

#include "offset/private/offset_algorithm.h"
#include "offset/private/offset_group.h"
#include "offset/private/offset_state.h"

#include <vector>

namespace clipper2next::internal {

auto inflate_paths_with_state(offset_state& state,
                              const Paths64& paths,
                              double delta,
                              JoinType join_type,
                              EndType end_type,
                              double miter_limit,
                              double arc_tolerance) -> Paths64 {
    if (delta == 0.0) { return paths; }

    state.reset();
    std::vector<offset_group> groups;
    groups.emplace_back(paths, join_type, end_type);
    Paths64 solution;
    execute_offset_algorithm(state,
                             groups,
                             delta,
                             solution,
                             nullptr,
                             offset_algorithm_options{
                                 .miter_limit = miter_limit,
                                 .arc_tolerance = arc_tolerance,
                             },
                             nullptr);
    return solution;
}

auto inflate_paths_with_scratch(offset_state& state,
                                std::vector<offset_group>& groups,
                                const Paths64& paths,
                                double delta,
                                JoinType join_type,
                                EndType end_type,
                                double miter_limit,
                                double arc_tolerance) -> Paths64 {
    groups.clear();
    if (delta == 0.0) { return paths; }

    state.reset();
    groups.emplace_back(paths, join_type, end_type);
    Paths64 solution;
    execute_offset_algorithm(state,
                             groups,
                             delta,
                             solution,
                             nullptr,
                             offset_algorithm_options{
                                 .miter_limit = miter_limit,
                                 .arc_tolerance = arc_tolerance,
                             },
                             nullptr);
    return solution;
}

}  // namespace clipper2next::internal
