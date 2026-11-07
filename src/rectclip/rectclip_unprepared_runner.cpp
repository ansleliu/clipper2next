#include "rectclip/private/rectclip_unprepared_runner.h"
#include "rectclip/private/rectclip_facade_runner.h"
#include "rectclip/private/rectclip_path_bounds.h"
#include "rectclip/private/rectclip_thread_state.h"
#include "support/private/paths_rebuild.h"
#include <utility>
#include <vector>

namespace clipper2next::internal {
auto execute_rectclip_unprepared_polygons(const Rect64& rect,
                                          const Paths64& paths,
                                          bool check_range) -> rectclip_unprepared_result {
    if (check_range && !rectclip_rect_in_range(rect)) { return {{}, false}; }
    if (!check_range && rect.is_empty()) { return {{}, true}; }
    rectclip_operation_lease operation;
    if (!check_range) {
        return {build_rectclip_unprepared_polygon_paths(operation.context(rect), paths), true};
    }
    auto& bounds = operation.bounds();
    if (!build_rectclip_path_bounds(paths, true, bounds)) { return {{}, false}; }
    if (rect.is_empty()) { return {{}, true}; }
    return {build_rectclip_paths(operation.context(rect), paths, bounds, rectclip_mode::polygons), true};
}

auto execute_rectclip_unprepared_polygons_into(const Rect64& rect,
                                               const Paths64& paths,
                                               bool check_range,
                                               Paths64& output) -> bool {
    auto completed = execute_rectclip_unprepared_polygons(rect, paths, check_range);
    if (!completed.in_range) { return false; }
    materialize_paths(std::move(completed.paths), output);
    return true;
}
}  // namespace clipper2next::internal
