#pragma once

#include "rectclip/private/rectclip_facade_runner.h"
#include "rectclip/private/rectclip_path_bounds.h"
#include "rectclip/private/rectclip_thread_state.h"

namespace clipper2next::tests {

using rectclip_mode = internal::rectclip_mode;

[[nodiscard]] inline auto execute_rectclip(
    const Rect64& rect, const Paths64& paths, std::span<const Rect64> bounds,
    rectclip_mode mode) -> Paths64 {
    Paths64 result;
    internal::rectclip_operation_lease operation;
    if (mode == rectclip_mode::lines) {
        internal::execute_rectclip_lines_into(rect, paths, bounds, result);
    } else {
        internal::execute_rectclip_into(operation.context(rect), paths, bounds, rectclip_mode::polygons, result);
    }
    return result;
}

[[nodiscard]] inline auto execute_rectclip(
    const Rect64& rect, const Paths64& paths, rectclip_mode mode) -> Paths64 {
    std::vector<Rect64> bounds;
    static_cast<void>(internal::build_rectclip_path_bounds(paths, false, bounds));
    return execute_rectclip(rect, paths, bounds, mode);
}

}  // namespace clipper2next::tests
