#include "clip/private/boolean_union_service.h"

#include "clip/private/clip_execution_strategy.h"
#include "clip/private/clip_request_validation.h"
#include "clipper2next/geometry.h"
#include "support/private/checked_size.h"
#include "support/private/union_component_grouping.h"

#include <utility>
#include <vector>

namespace clipper2next::internal {
namespace {

constexpr std::size_t component_union_path_threshold = 2U;

[[nodiscard]] auto union_paths_direct(Paths64&& paths, const clip_union_options& options)
    -> paths64_result {
    clip_request64 request;
    request.clip_type = ClipType::Union;
    request.fill_rule = options.fill_rule;
    request.subjects = std::move(paths);
    request.options = options.options;
    if (!clip_request_in_range(request)) { return {}; }
    if (options.use_offset_cleanup_monotone_scanbeam_runs) {
        return execute_clip_validated_for_offset_cleanup(request);
    }
    return execute_clip_validated(request);
}

[[nodiscard]] auto component_bounds(const Paths64& paths) -> std::vector<Rect64> {
    std::vector<Rect64> path_bounds;
    path_bounds.reserve(paths.size());
    for (const auto& path : paths) {
        path_bounds.emplace_back(path.empty() ? Rect64::invalid_rect() : bounds(path));
    }
    return path_bounds;
}

[[nodiscard]] auto non_empty_component_count(
    const std::vector<std::vector<std::size_t>>& components) -> std::size_t {
    std::size_t count = 0;
    for (const auto& component : components) {
        if (!component.empty()) { ++count; }
    }
    return count;
}

// Moves only the paths belonging to this component out of `paths`; components
// partition the indices, so each element is consumed exactly once.
auto append_union_component(Paths64& paths,
                            const std::vector<std::size_t>& component,
                            const clip_union_options& options,
                            paths64_result& result) -> void {
    if (component.size() == 1U) {
        const auto index = component.front();
        if (try_append_singleton_component_direct(paths[index], options, result)) { return; }
    }

    Paths64 component_paths;
    component_paths.reserve(component.size());
    for (const auto index : component) { component_paths.emplace_back(std::move(paths[index])); }
    auto component_result = union_paths_direct(std::move(component_paths), options);
    result.closed.reserve(
        checked_size_add(result.closed.size(), component_result.closed.size()));
    for (auto& path : component_result.closed) { result.closed.emplace_back(std::move(path)); }
    result.open.reserve(checked_size_add(result.open.size(), component_result.open.size()));
    for (auto& path : component_result.open) { result.open.emplace_back(std::move(path)); }
}

[[nodiscard]] auto union_disjoint_bbox_components(Paths64&& paths,
                                                  const clip_union_options& options)
    -> paths64_result {
    if (paths.size() == 1U) {
        paths64_result result;
        if (try_append_singleton_component_direct(paths.front(), options, result)) {
            return result;
        }
        return union_paths_direct(std::move(paths), options);
    }
    if (!options.decompose_disjoint_components) {
        return union_paths_direct(std::move(paths), options);
    }
    if (paths.size() < component_union_path_threshold) {
        return union_paths_direct(std::move(paths), options);
    }

    const auto path_bounds = component_bounds(paths);
    auto components = group_union_components_by_bounds(path_bounds);
    if (non_empty_component_count(components) <= 1U) {
        return union_paths_direct(std::move(paths), options);
    }

    paths64_result result;
    result.closed.reserve(paths.size());
    for (const auto& component : components) {
        if (!component.empty()) { append_union_component(paths, component, options, result); }
    }
    return result;
}

}  // namespace

auto union_paths(const Paths64& paths, const clip_union_options& options) -> paths64_result {
    Paths64 copied_paths;
    copied_paths.reserve(paths.size());
    for (const auto& path : paths) { copied_paths.emplace_back(path); }
    return union_disjoint_bbox_components(std::move(copied_paths), options);
}

auto union_paths(Paths64&& paths, const clip_union_options& options) -> paths64_result {
    return union_disjoint_bbox_components(std::move(paths), options);
}

auto union_closed_paths(const Paths64& paths, const clip_union_options& options) -> Paths64 {
    return union_paths(paths, options).closed;
}

auto union_closed_paths(Paths64&& paths, const clip_union_options& options) -> Paths64 {
    return union_paths(std::move(paths), options).closed;
}

auto union_closed_paths_into_tree(const Paths64& paths,
                                  PolyTree64& tree,
                                  const clip_union_options& options) -> void {
    tree.clear();
    for (const auto& path : union_closed_paths(paths, options)) {
        static_cast<void>(tree.add_child(tree.root(), path));
    }
}

}  // namespace clipper2next::internal
