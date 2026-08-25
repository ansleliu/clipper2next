#pragma once

#include "clipper2next/clipper.h"
#include "clipper2next/geometry/algorithms.h"

namespace clipper2next::internal {

[[nodiscard]] inline auto translate_paths64(const Paths64& paths, int64_t dx, int64_t dy)
    -> Paths64 {
    return translate(paths, dx, dy);
}

// Safe acceleration boundary: bounds only performs independent min/max reductions.
// Predicates that depend on exact winding, intersection ordering, area accumulation,
// or integer overflow behavior stay on the scalar public implementations.
[[nodiscard]] inline auto bounds(const Path64& path) -> Rect64 {
    return clipper2next::bounds(path);
}

[[nodiscard]] inline auto bounds_of(const Paths64& paths) -> Rect64 {
    return clipper2next::bounds(paths);
}

[[nodiscard]] inline auto simplify_path_rdp(const Path64& path, double epsilon) -> Path64 {
    return ramer_douglas_peucker(path, epsilon);
}

}  // namespace clipper2next::internal
