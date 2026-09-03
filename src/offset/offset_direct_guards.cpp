#include "offset/private/offset_algorithm.h"

#include "clipper2next/geometry.h"
#include "clipper2next/geometry/algorithms.h"
#include "clipper2next/geometry/core.h"
#include "geometry/private/path_simplicity.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace clipper2next::internal {
namespace {

constexpr std::size_t direct_simple_path_scan_limit = 64U;
constexpr std::size_t direct_disjoint_simple_pairwise_path_count_limit = 16U;
constexpr std::size_t direct_disjoint_simple_path_count_limit = 256U;
constexpr std::size_t prepared_disjoint_path_count_limit = 4096U;
constexpr std::size_t prepared_simple_path_scan_limit = 512U;

[[nodiscard]] auto paths_are_pairwise_bbox_disjoint_quadratic(
    const std::vector<Rect64>& path_bounds) -> bool {
    for (std::size_t first = 0; first < path_bounds.size(); ++first) {
        for (std::size_t second = first + 1U; second < path_bounds.size(); ++second) {
            if (path_bounds[first].intersects(path_bounds[second])) { return false; }
        }
    }
    return true;
}

[[nodiscard]] auto paths_are_pairwise_bbox_disjoint_sweep(const std::vector<Rect64>& path_bounds)
    -> bool {
    auto ordered = std::vector<std::size_t>(path_bounds.size());
    std::iota(ordered.begin(), ordered.end(), std::size_t{});
    std::ranges::sort(
        ordered,
        [&path_bounds](const std::size_t first, const std::size_t second) {
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
    auto active = std::vector<std::size_t>{};
    active.reserve(path_bounds.size());
    for (const auto current : ordered) {
        const auto& bounds = path_bounds[current];
        std::erase_if(active, [&](const std::size_t index) {
            return path_bounds[index].right < bounds.left;
        });
        for (const auto index : active) {
            if (path_bounds[index].intersects(bounds)) {
                return false;
            }
        }
        active.push_back(current);
    }
    return true;
}

[[nodiscard]] auto paths_are_pairwise_bbox_disjoint(const std::vector<Rect64>& path_bounds)
    -> bool {
    if (path_bounds.size() <= direct_disjoint_simple_pairwise_path_count_limit) {
        return paths_are_pairwise_bbox_disjoint_quadratic(path_bounds);
    }
    return paths_are_pairwise_bbox_disjoint_sweep(path_bounds);
}

template <typename PathLike>
[[nodiscard]] auto path_matches_direct_fill_orientation(const PathLike& path,
                                                        bool paths_reversed) -> bool {
    return is_positive(std::span<const Point64>{path.data(), path.size()}) != paths_reversed;
}

template <typename Solution>
[[nodiscard]] auto all_paths_are_simple_and_pairwise_disjoint(const Solution& solution,
                                                              bool paths_reversed) -> bool {
    if (solution.size() < 2U || solution.size() > direct_disjoint_simple_path_count_limit) {
        return false;
    }

    std::vector<Rect64> path_bounds;
    path_bounds.reserve(solution.size());
    for (const auto& path : solution) {
        if (path.size() < 3U || path.size() > direct_simple_path_scan_limit ||
            !path_matches_direct_fill_orientation(path, paths_reversed) ||
            !path_simplicity::path_is_provably_simple(path, direct_simple_path_scan_limit)) {
            return false;
        }
        auto accumulator = bounds_accumulator<std::int64_t>{};
        for (const auto& point : path) { accumulator.include(point); }
        path_bounds.emplace_back(accumulator.rect());
    }
    return paths_are_pairwise_bbox_disjoint(path_bounds);
}

}  // namespace

auto can_return_direct_convex_offset(const std::vector<offset_group>& groups,
                                     double delta,
                                     PolyTree64* solution_tree,
                                     const offset_algorithm_options& options,
                                     bool paths_reversed) -> bool {
    return solution_tree == nullptr && delta > 0.0 && !options.preserve_collinear &&
           !options.reverse_solution && !paths_reversed && groups.size() == 1 &&
           groups.front().end_type == EndType::Polygon && groups.front().path_count() == 1U &&
           path_simplicity::is_convex_simple_polygon(groups.front().path(0U));
}

template <typename Solution>
auto can_return_direct_simple_offset_impl(const std::vector<offset_group>& groups,
                                          const Solution& solution,
                                     double delta,
                                     PolyTree64* solution_tree,
                                     const offset_algorithm_options& options,
                                     bool paths_reversed) -> bool {
    return solution_tree == nullptr && delta > 0.0 && !options.preserve_collinear &&
           groups.size() == 1 &&
           groups.front().end_type == EndType::Polygon && groups.front().path_count() == 1U &&
           solution.size() == 1U &&
           solution.front().size() <= direct_simple_path_scan_limit &&
           path_matches_direct_fill_orientation(solution.front(), paths_reversed) &&
           path_simplicity::path_is_provably_simple(solution.front(),
                                                    direct_simple_path_scan_limit);
}

auto can_return_direct_simple_offset(const std::vector<offset_group>& groups,
                                     const Paths64& solution,
                                     double delta,
                                     PolyTree64* solution_tree,
                                     const offset_algorithm_options& options,
                                     bool paths_reversed) -> bool {
    return can_return_direct_simple_offset_impl(
        groups, solution, delta, solution_tree, options, paths_reversed);
}

auto can_return_direct_simple_offset(const std::vector<offset_group>& groups,
                                     const path_set64& solution,
                                     double delta,
                                     PolyTree64* solution_tree,
                                     const offset_algorithm_options& options,
                                     bool paths_reversed) -> bool {
    return can_return_direct_simple_offset_impl(
        groups, solution, delta, solution_tree, options, paths_reversed);
}

auto can_return_direct_disjoint_simple_offset(const std::vector<offset_group>& groups,
                                              const Paths64& solution,
                                              double delta,
                                              PolyTree64* solution_tree,
                                              const offset_algorithm_options& options,
                                              bool paths_reversed) -> bool {
    return solution_tree == nullptr && delta > 0.0 && !options.preserve_collinear &&
           groups.size() == 1 &&
           groups.front().end_type == EndType::Polygon &&
           all_paths_are_simple_and_pairwise_disjoint(solution, paths_reversed);
}

auto try_prepare_direct_disjoint_simple_offset(
    const std::vector<offset_group>& groups,
    path_set64& solution,
    const double delta,
    const offset_algorithm_options& options,
    const bool paths_reversed) -> bool {
    if (delta <= 0.0 || options.preserve_collinear ||
        groups.size() != 1U ||
        groups.front().end_type != EndType::Polygon ||
        solution.size() < 2U ||
        solution.size() > prepared_disjoint_path_count_limit) {
        return false;
    }

    auto prepared = path_set64{};
    prepared.reserve(solution.size(), solution.point_count());
    auto path_bounds = std::vector<Rect64>{};
    path_bounds.reserve(solution.size());
    for (const auto path : solution) {
        auto materialized = Path64{path.begin(), path.end()};
        auto candidate = trim_collinear(materialized, false);
        if (candidate.size() < 3U ||
            !path_matches_direct_fill_orientation(
                candidate, paths_reversed) ||
            !path_simplicity::path_is_provably_simple(
                candidate, prepared_simple_path_scan_limit)) {
            return false;
        }
        auto accumulator = bounds_accumulator<std::int64_t>{};
        for (const auto& point : candidate) {
            accumulator.include(point);
        }
        path_bounds.emplace_back(accumulator.rect());
        prepared.append(
            candidate, geotypes::PathClosure::ClosedImplicit);
    }
    if (!paths_are_pairwise_bbox_disjoint(path_bounds)) {
        return false;
    }
    solution = std::move(prepared);
    return true;
}

auto can_return_direct_disjoint_simple_offset(const std::vector<offset_group>& groups,
                                              const path_set64& solution,
                                              double delta,
                                              PolyTree64* solution_tree,
                                              const offset_algorithm_options& options,
                                              bool paths_reversed) -> bool {
    return solution_tree == nullptr && delta > 0.0 && !options.preserve_collinear &&
           groups.size() == 1 &&
           groups.front().end_type == EndType::Polygon &&
           all_paths_are_simple_and_pairwise_disjoint(solution, paths_reversed);
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


auto canonicalize_direct_offset_solution(path_set64& solution,
                                         bool reverse_solution) -> void {
    for (std::size_t index = 0; index < solution.size(); ++index) {
        auto path = solution.mutable_path(index);
        if (reverse_solution) { std::ranges::reverse(path); }
        const auto first = std::ranges::max_element(
            path,
            [](const Point64& lhs, const Point64& rhs) {
                return lhs.x < rhs.x || (lhs.x == rhs.x && lhs.y < rhs.y);
            });
        if (first != path.end()) { std::ranges::rotate(path, first); }
    }
}

}  // namespace clipper2next::internal
