#include "clip/private/clip_execution_strategy.h"

#include "clip/private/closed_clip_fast_path.h"
#include "clipper2next/clip.h"

namespace clipper2next::internal {

auto execute_clip_with_fast_path_validated(const clip_request64& request) -> paths64_result {
    if (!request.open_subjects.empty()) { return execute_clip_validated(request); }

    paths64_result result;
    if (try_execute_closed_clip_fast_path(request, result)) { return result; }
    return execute_clip_validated(request);
}

auto execute_clip_with_fast_path_validated(const clip_request64& request,
                                           const clip_request_metadata64& metadata)
    -> paths64_result {
    paths64_result result;
    if (try_execute_closed_clip_fast_path(request, metadata, result)) { return result; }
    return execute_clip_validated(request);
}

auto execute_clip_into_with_fast_path(const clip_request64& request, paths64_result& result)
    -> void {
    if (try_execute_closed_clip_fast_path(request, result)) { return; }
    execute_clip_into_validated(request, result);
}

}  // namespace clipper2next::internal
