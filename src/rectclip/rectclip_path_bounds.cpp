#include "rectclip/private/rectclip_path_bounds.h"
#include "rectclip/private/rectclip_unprepared_bounds.h"

namespace clipper2next::internal {

auto rectclip_rect_in_range(const Rect64& rect) -> bool {
    return rectclip_coordinate_in_range(rect.left) && rectclip_coordinate_in_range(rect.top) &&
           rectclip_coordinate_in_range(rect.right) && rectclip_coordinate_in_range(rect.bottom);
}

}  // namespace clipper2next::internal
