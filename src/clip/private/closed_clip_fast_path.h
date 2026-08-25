#pragma once

#include "clipper2next/clip/request.h"

namespace clipper2next::internal {

[[nodiscard]] auto is_likely_closed_clip_fast_path_candidate(const clip_request64& request,
                                                             const clip_request_metadata64& metadata) -> bool;

[[nodiscard]] auto try_execute_closed_clip_fast_path(const clip_request64& request,
                                                     paths64_result& result) -> bool;
[[nodiscard]] auto try_execute_closed_clip_fast_path(const clip_request64& request,
                                                     const clip_request_metadata64& metadata,
                                                     paths64_result& result) -> bool;
[[nodiscard]] auto try_execute_closed_clip_fast_path(const prepared_clip_request64& request,
                                                     paths64_result& result) -> bool;

}  // namespace clipper2next::internal
