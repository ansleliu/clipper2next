#pragma once

#include "offset/private/offset_algorithm.h"

namespace clipper2next::internal {

[[nodiscard]] auto can_return_direct_simple_offset(const std::vector<offset_group>& groups,
                                                   const Paths64& solution,
                                                   double delta,
                                                   PolyTree64* solution_tree,
                                                   const offset_algorithm_options& options,
                                                   bool paths_reversed) -> bool;
[[nodiscard]] auto can_return_direct_simple_offset(const std::vector<offset_group>& groups,
                                                   const path_set64& solution,
                                                   double delta,
                                                   PolyTree64* solution_tree,
                                                   const offset_algorithm_options& options,
                                                   bool paths_reversed) -> bool;
[[nodiscard]] auto can_return_direct_disjoint_simple_offset(const std::vector<offset_group>& groups,
                                                            const Paths64& solution,
                                                            double delta,
                                                            PolyTree64* solution_tree,
                                                            const offset_algorithm_options& options,
                                                            bool paths_reversed) -> bool;
[[nodiscard]] auto try_prepare_direct_disjoint_simple_offset(
    const std::vector<offset_group>& groups,
    path_set64& solution,
    double delta,
    const offset_algorithm_options& options,
    bool paths_reversed) -> bool;
[[nodiscard]] auto can_return_direct_disjoint_simple_offset(const std::vector<offset_group>& groups,
                                                            const path_set64& solution,
                                                            double delta,
                                                            PolyTree64* solution_tree,
                                                            const offset_algorithm_options& options,
                                                            bool paths_reversed) -> bool;
auto canonicalize_direct_offset_solution(Paths64& solution, bool reverse_solution) -> void;
auto canonicalize_direct_offset_solution(path_set64& solution, bool reverse_solution) -> void;

[[nodiscard]] auto offset_groups_in_range(const std::vector<offset_group>& groups) noexcept -> bool;
[[nodiscard]] auto offset_solution_in_range(const Paths64& solution) noexcept -> bool;
[[nodiscard]] auto offset_solution_in_range(const path_set64& solution) noexcept -> bool;
[[nodiscard]] auto calc_solution_capacity(const std::vector<offset_group>& groups) -> std::size_t;
[[nodiscard]] auto check_reverse_orientation(const std::vector<offset_group>& groups) -> bool;

}  // namespace clipper2next::internal
