#pragma once

#include "offset/private/offset_algorithm.h"

namespace clipper2next::internal {

[[nodiscard]] auto can_return_direct_simple_offset(std::span<const offset_group> groups,
                                                   const Paths64& solution,
                                                   double delta,
                                                   PolyTree64* solution_tree,
                                                   const offset_algorithm_options& options,
                                                   bool paths_reversed) -> bool;
[[nodiscard]] auto can_return_direct_simple_offset(std::span<const offset_group> groups,
                                                   const path_set64& solution,
                                                   double delta,
                                                   PolyTree64* solution_tree,
                                                   const offset_algorithm_options& options,
                                                   bool paths_reversed) -> bool;
[[nodiscard]] auto can_return_direct_disjoint_simple_offset(std::span<const offset_group> groups,
                                                            const Paths64& solution,
                                                            double delta,
                                                            PolyTree64* solution_tree,
                                                            const offset_algorithm_options& options,
                                                            bool paths_reversed,
                                                            execution_resource_ledger* resources = nullptr) -> bool;
[[nodiscard]] auto try_prepare_direct_disjoint_simple_offset(
    std::span<const offset_group> groups,
    path_set64& solution,
    double delta,
    const offset_algorithm_options& options,
    bool paths_reversed,
    scoped_workspace_charge* solution_charge = nullptr) -> bool;

auto normalize_offset_group_output(Paths64& solution, std::size_t first,
                                   const offset_group& group, bool reference_reversed) -> void;
auto normalize_offset_group_output(path_set64& solution, std::size_t first,
                                   const offset_group& group, bool reference_reversed) -> void;
auto canonicalize_direct_offset_solution(Paths64& solution, bool reverse_solution) -> void;
auto canonicalize_direct_offset_solution(path_set64& solution, bool reverse_solution) -> void;

[[nodiscard]] auto offset_solution_in_range(const Paths64& solution) noexcept -> bool;
[[nodiscard]] auto offset_solution_in_range(const path_set64& solution) noexcept -> bool;
[[nodiscard]] auto calc_solution_capacity(std::span<const offset_group> groups) -> std::size_t;
[[nodiscard]] auto check_reverse_orientation(std::span<const offset_group> groups) -> bool;

}  // namespace clipper2next::internal
