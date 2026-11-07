#include "rectclip/private/rectclip_path_bounds.h"

namespace clipper2next::internal {

auto rectclip_rect_strictly_contains_bounds(
    const Rect64& rect, const Rect64& path_bounds) -> bool {
    return path_bounds.left > rect.left && path_bounds.top > rect.top &&
           path_bounds.right < rect.right && path_bounds.bottom < rect.bottom;
}

}  // namespace clipper2next::internal
