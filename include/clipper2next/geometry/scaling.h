#pragma once

#include "clipper2next/geometry/scale.h"

namespace clipper2next {

template <typename Target, typename Source>
[[nodiscard]] inline auto scale_path(const Path<Source>& path, const scale_request& request)
    -> clipper_result<Path<Target>> {
    return scale_path<Target, Source>(path, request.x, request.y);
}

template <typename Target, typename Source>
[[nodiscard]] inline auto scale_paths(const Paths<Source>& paths, const scale_request& request)
    -> clipper_result<Paths<Target>> {
    return scale_paths<Target, Source>(paths, request.x, request.y);
}

}  // namespace clipper2next
