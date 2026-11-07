#include "offset/private/offset_algorithm.h"
#include "clip/engine/private/engine_buffer.h"
#include "geometry/private/trim_collinear.h"

#include "clipper2next/geometry.h"
#include "clipper2next/geometry/algorithms.h"
#include "clipper2next/geometry/core.h"
#include "geometry/private/path_simplicity.h"
#include "geometry/private/integer_area_sign.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace clipper2next::internal {
namespace {

constexpr std::size_t direct_nonconvex_path_scan_limit = 64U;
constexpr std::size_t direct_disjoint_simple_pairwise_path_count_limit = 16U;
constexpr std::size_t direct_disjoint_simple_path_count_limit = 256U;
constexpr std::size_t prepared_disjoint_path_count_limit = 4096U;
constexpr std::size_t prepared_simple_path_scan_limit = 512U;

[[nodiscard]] auto paths_are_pairwise_bbox_disjoint_quadratic(
    const engine_buffer<Rect64>& path_bounds) -> bool {
    for (std::size_t first = 0; first < path_bounds.size(); ++first) {
        for (std::size_t second = first + 1U; second < path_bounds.size(); ++second) {
            if (path_bounds[first].intersects(path_bounds[second])) { return false; }
        }
    }
    return true;
}

[[nodiscard]] auto paths_are_pairwise_bbox_disjoint_sweep(const engine_buffer<Rect64>& path_bounds, execution_resource_ledger* resources)
    -> bool {
    auto ordered = engine_buffer<std::size_t>{};
    ordered.bind(resources);
    ordered.resize(path_bounds.size());
    std::iota(ordered.begin(), ordered.end(), std::size_t{});
    std::ranges::sort(ordered, [&path_bounds](const std::size_t first, const std::size_t second) {
        const auto& first_bounds = path_bounds[first];
        const auto& second_bounds = path_bounds[second];
        if (first_bounds.left != second_bounds.left) {
            return first_bounds.left < second_bounds.left;
        }
        if (first_bounds.right != second_bounds.right) {
            return first_bounds.right < second_bounds.right;
        }
        return first < second;
    });
    auto active = engine_buffer<std::size_t>{};
    active.bind(resources);
    active.reserve(path_bounds.size());
    for (const auto current : ordered) {
        const auto& bounds = path_bounds[current];
        active.erase(std::remove_if(active.begin(), active.end(), [&](const std::size_t index) {
            return path_bounds[index].right < bounds.left;
        }), active.end());
        for (const auto index : active) {
            if (path_bounds[index].intersects(bounds)) { return false; }
        }
        active.push_back(current);
    }
    return true;
}

[[nodiscard]] auto paths_are_pairwise_bbox_disjoint(const engine_buffer<Rect64>& path_bounds, execution_resource_ledger* resources)
    -> bool {
    if (path_bounds.size() <= direct_disjoint_simple_pairwise_path_count_limit) {
        return paths_are_pairwise_bbox_disjoint_quadratic(path_bounds);
    }
    return paths_are_pairwise_bbox_disjoint_sweep(path_bounds, resources);
}

template <typename PathLike>
[[nodiscard]] auto path_is_simple_with_direct_fill_orientation(const PathLike& path,
                                                               bool paths_reversed,
                                                               std::size_t scan_limit) -> bool {
    const auto convex_sign = path_simplicity::convex_simple_orientation(path);
    if (convex_sign != 0) { return paths_reversed ? convex_sign < 0 : convex_sign > 0; }
    const auto sign = signed_area_sign(std::span<const Point64>{path.data(), path.size()});
    const auto matches = paths_reversed ? sign < 0 : sign > 0;
    return matches && !path_simplicity::has_boundary_self_intersection(path, scan_limit);
}

enum class disjoint_path_assessment { unproved, disjoint, overlapping_bounds };

template <typename Solution>
[[nodiscard]] auto assess_disjoint_simple_paths(const Solution& solution,
                                                              bool paths_reversed, execution_resource_ledger* resources) -> disjoint_path_assessment {
    if (solution.size() < 2U || solution.size() > direct_disjoint_simple_path_count_limit) {
        return disjoint_path_assessment::unproved;
    }

    engine_buffer<Rect64> path_bounds;
    path_bounds.bind(resources);
    path_bounds.reserve(solution.size());
    for (const auto& path : solution) {
        if (path.size() < 3U ||
            !path_is_simple_with_direct_fill_orientation(path, paths_reversed,
                                                        direct_nonconvex_path_scan_limit)) {
            return disjoint_path_assessment::unproved;
        }
        auto accumulator = bounds_accumulator<std::int64_t>{};
        for (const auto& point : path) { accumulator.include(point); }
        path_bounds.emplace_back(accumulator.rect());
    }
    return paths_are_pairwise_bbox_disjoint(path_bounds, resources)
        ? disjoint_path_assessment::disjoint
        : disjoint_path_assessment::overlapping_bounds;
}

}  // namespace

template <typename Solution>
auto can_return_direct_simple_offset_impl(std::span<const offset_group> groups,
                                          const Solution& solution,
                                          double delta,
                                          PolyTree64* solution_tree,
                                          const offset_algorithm_options& options,
                                          bool paths_reversed) -> bool {
    return solution_tree == nullptr && delta > 0.0 && !options.preserve_collinear &&
           groups.size() == 1 && groups.front().end_type == EndType::Polygon &&
           groups.front().path_count() == 1U && solution.size() == 1U &&
           path_is_simple_with_direct_fill_orientation(solution.front(), paths_reversed,
                                                       direct_nonconvex_path_scan_limit);
}

auto can_return_direct_simple_offset(std::span<const offset_group> groups,
                                     const Paths64& solution,
                                     double delta,
                                     PolyTree64* solution_tree,
                                     const offset_algorithm_options& options,
                                     bool paths_reversed) -> bool {
    return can_return_direct_simple_offset_impl(
        groups, solution, delta, solution_tree, options, paths_reversed);
}

auto can_return_direct_simple_offset(std::span<const offset_group> groups,
                                     const path_set64& solution,
                                     double delta,
                                     PolyTree64* solution_tree,
                                     const offset_algorithm_options& options,
                                     bool paths_reversed) -> bool {
    return can_return_direct_simple_offset_impl(
        groups, solution, delta, solution_tree, options, paths_reversed);
}

auto can_return_direct_disjoint_simple_offset(std::span<const offset_group> groups,
                                              const Paths64& solution,
                                              double delta,
                                              PolyTree64* solution_tree,
                                              const offset_algorithm_options& options,
                                              bool paths_reversed, execution_resource_ledger* resources) -> bool {
    return solution_tree == nullptr && delta > 0.0 && !options.preserve_collinear &&
           groups.size() == 1 && groups.front().end_type == EndType::Polygon &&
           assess_disjoint_simple_paths(solution, paths_reversed, resources) ==
               disjoint_path_assessment::disjoint;
}

auto try_prepare_direct_disjoint_simple_offset(std::span<const offset_group> groups,
                                               path_set64& solution,
                                               const double delta,
                                               const offset_algorithm_options& options,
                                               const bool paths_reversed, scoped_workspace_charge* solution_charge) -> bool {
    if (delta <= 0.0 || options.preserve_collinear || groups.size() != 1U ||
        groups.front().end_type != EndType::Polygon || solution.size() < 2U ||
        solution.size() > prepared_disjoint_path_count_limit) {
        return false;
    }

    auto* resources = solution_charge ? solution_charge->context() : nullptr;
    const auto assessment = assess_disjoint_simple_paths(solution, paths_reversed, resources);
    if (assessment != disjoint_path_assessment::unproved) {
        // Removing interior collinear vertices from an already-simple boundary
        // preserves its extrema. Overlapping bounds cannot become disjoint
        // through preparation, so neither result needs another owned copy.
        return assessment == disjoint_path_assessment::disjoint;
    }
    auto prepared_charge = scoped_workspace_charge{resources};
    auto prepared = path_set64{};
    prepared.reserve(solution.size(), solution.point_count());
    auto path_bounds = engine_buffer<Rect64>{};
    path_bounds.bind(resources);
    path_bounds.reserve(solution.size());
    for (const auto path : solution) {
        auto candidate = engine_buffer<Point64>{};
        candidate.bind(resources);
        // Raw output has passed offset_solution_in_range before direct assessment.
        trim_collinear_into(path, false, candidate,
            [](const Point64& first, const Point64& shared, const Point64& second) {
                return cross_product_sign_in_clipper_range(first, shared, second) == 0;
            });
        if (candidate.size() < 3U ||
            !path_is_simple_with_direct_fill_orientation(candidate, paths_reversed,
                                                        prepared_simple_path_scan_limit)) {
            return false;
        }
        auto accumulator = bounds_accumulator<std::int64_t>{};
        for (const auto& point : candidate) { accumulator.include(point); }
        path_bounds.emplace_back(accumulator.rect());
        auto growth = scoped_workspace_growth{prepared_charge, checked_size_add(prepared_charge.bytes(), checked_size_add(checked_size_multiply(candidate.size(), sizeof(Point64)), sizeof(geotypes::PathDescriptor))), 1U};
        prepared.append(std::span<const Point64>{candidate.data(), candidate.size()}, geotypes::PathClosure::ClosedImplicit);
        growth.commit();
    }
    if (!paths_are_pairwise_bbox_disjoint(path_bounds, resources)) { return false; }
    solution = std::move(prepared);
    if (solution_charge) { *solution_charge = std::move(prepared_charge); }
    return true;
}


auto canonicalize_direct_offset_solution(Paths64& solution, bool reverse_solution) -> void {
    for (auto& path : solution) {
        if (reverse_solution) { std::reverse(path.begin(), path.end()); }
        const auto first =
            std::max_element(path.begin(), path.end(), [](const Point64& lhs, const Point64& rhs) {
                return lhs.x < rhs.x || (lhs.x == rhs.x && lhs.y < rhs.y);
            });
        if (first != path.end()) { std::rotate(path.begin(), first, path.end()); }
    }
}

auto canonicalize_direct_offset_solution(path_set64& solution, bool reverse_solution) -> void {
    for (std::size_t index = 0; index < solution.size(); ++index) {
        auto path = solution.mutable_path(index);
        if (reverse_solution) { std::ranges::reverse(path); }
        const auto first =
            std::ranges::max_element(path, [](const Point64& lhs, const Point64& rhs) {
                return lhs.x < rhs.x || (lhs.x == rhs.x && lhs.y < rhs.y);
            });
        if (first != path.end()) { std::ranges::rotate(path, first); }
    }
}

}  // namespace clipper2next::internal
