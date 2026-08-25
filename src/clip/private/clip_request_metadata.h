#pragma once

#include "clipper2next/clip/request.h"

namespace clipper2next::internal {

[[nodiscard]] auto build_clip_request_metadata(const clip_request64& request)
    -> clip_request_metadata64;

}  // namespace clipper2next::internal
