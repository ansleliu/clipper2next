#include "clipper2next/clip.h"

#include <algorithm>
#include <array>
#include <optional>

namespace clipper2next::internal {
namespace {

[[nodiscard]] auto path_as_axis_aligned_rect(const Path64& path) -> std::optional<Rect64> {
    if (path.size() != 4U) { return std::nullopt; }
    const auto rect = bounds(path);
    if (rect.is_empty()) { return std::nullopt; }
    const std::array<Point64, 4U> corners{Point64{rect.left, rect.top},
                                          Point64{rect.right, rect.top},
                                          Point64{rect.right, rect.bottom},
                                          Point64{rect.left, rect.bottom}};
    for (const auto& corner : corners) {
        if (std::find(path.begin(), path.end(), corner) == path.end()) { return std::nullopt; }
    }
    return rect;
}

}  // namespace

auto try_execute_nested_rectangle_union_tree(const clip_request64& request,
                                             clip_tree64_result& result) -> bool {
    if (request.clip_type != ClipType::Union || request.fill_rule != FillRule::NonZero ||
        request.options.reverse_solution || !request.clips.empty() ||
        !request.open_subjects.empty() || request.subjects.empty()) {
        return false;
    }

    const auto outer_rect = path_as_axis_aligned_rect(request.subjects.front());
    if (!outer_rect) { return false; }
    const auto outer_is_positive = is_positive(request.subjects.front());
    for (const auto& path : request.subjects) {
        const auto rect = path_as_axis_aligned_rect(path);
        if (!rect || !outer_rect->contains(*rect) || is_positive(path) != outer_is_positive) {
            return false;
        }
    }

    result.tree.clear();
    (void)result.tree.add_child(result.tree.root(), request.subjects.front());
    result.open.clear();
    return true;
}

}  // namespace clipper2next::internal
