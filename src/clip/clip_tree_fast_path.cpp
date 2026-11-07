#include "clipper2next/clip.h"

#include "clip/engine/private/polygon_tree_rebuild_access.h"
#include "geometry/private/rectangle_certificate.h"
#include <algorithm>
#include <array>

namespace clipper2next::internal {
auto try_execute_nested_rectangle_union_tree(const clip_request64& request,
                                             clip_tree64_result& result) -> bool {
    if (request.clip_type != ClipType::Union || request.fill_rule != FillRule::NonZero ||
        request.options.reverse_solution || !request.clips.empty() ||
        !request.open_subjects.empty() || request.subjects.empty()) {
        return false;
    }

    const auto outer = classify_axis_aligned_rectangle(request.subjects.front());
    if (!outer) { return false; }
    for (std::size_t index = 1; index < request.subjects.size(); ++index) {
        const auto rectangle = classify_axis_aligned_rectangle(request.subjects[index]);
        if (!rectangle || rectangle->positive != outer->positive ||
            (rectangle->bounds != outer->bounds &&
             !rectangle_strictly_contains(outer->bounds, rectangle->bounds))) {
            return false;
        }
    }

    // The fixed rectangle coordinates are fully consumed before storage changes.
    const auto& source = request.subjects.front();
    std::array<Point64, 4> polygon{source[0], source[1], source[2], source[3]};
    if (!outer->positive) { std::reverse(polygon.begin(), polygon.end()); }
    struct layout_item {
        std::size_t point_count;
        std::size_t child_count;
    };
    const std::array<layout_item, 2> layout{{{0, 1}, {4, 0}}};
    using access = polygon_tree_rebuild_access;
    access::prepare(result.tree, layout);
    access::begin(result.tree);
    try {
        auto& path = access::polygon(result.tree, 1);
        path.insert(path.end(), polygon.begin(), polygon.end());
        access::link(result.tree, 1, 0, 1);
        access::resize(result.tree, 2);
    } catch (...) {
        access::cancel(result.tree);
        result.open.clear();
        throw;
    }
    result.open.clear();
    return true;
}

}  // namespace clipper2next::internal
