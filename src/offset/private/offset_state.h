#pragma once

#include "clipper2next/core.h"
#include "offset/private/offset_geometry.h"

namespace clipper2next::internal {

struct offset_state final {
    double delta{0.0};
    double group_delta{0.0};
    double temp_limit{0.0};
    offset_arc_parameters arc{};
    PathD normals{};
    Path64 path_out{};
    Path64 callback_path{};

    auto reset() noexcept -> void {
        delta = 0.0;
        group_delta = 0.0;
        temp_limit = 0.0;
        arc = {};
        normals.clear();
        path_out.clear();
        callback_path.clear();
    }

    auto release() noexcept -> void {
        reset();
        PathD{}.swap(normals);
        Path64{}.swap(path_out);
        Path64{}.swap(callback_path);
    }
};

}  // namespace clipper2next::internal
