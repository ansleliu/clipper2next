#pragma once

#include "clipper2next/core.h"

namespace clipper2next::internal {
auto execute_rectclip_unprepared_polygons_into(const Rect64&, const Paths64&, bool, Paths64&)
    -> bool;

struct rectclip_unprepared_result final {
    Paths64 paths{};
    bool in_range = true;
};

[[nodiscard]] auto execute_rectclip_unprepared_polygons(
    const Rect64& rect,
    const Paths64& paths,
    bool check_coordinate_range)
    -> rectclip_unprepared_result;

}  // namespace clipper2next::internal
