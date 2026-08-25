#include "clip/private/clip_execution_strategy.h"

#include "clip/private/closed_clip_fast_path.h"
#include "clip/private/clip_request_validation.h"
#include "clipper2next/clip.h"

namespace clipper2next::internal {

auto execute_clip_with_fast_path(const clip_request64& request) -> paths64_result {
    return execute_clip_with_fast_path_validated(request);
}

auto execute_clip_with_fast_path_validated(const clip_request64& request) -> paths64_result {
    if (!request.open_subjects.empty()) { return execute_clip_validated(request); }

    paths64_result result;
    if (try_execute_closed_clip_fast_path(request, result)) { return result; }
    return execute_clip_validated(request);
}

auto execute_clip_with_fast_path(const clip_request64& request,
                                 const clip_request_metadata64& metadata) -> paths64_result {
    paths64_result result;
    if (!clip_request_in_range(request) || !clip_metadata_in_range(metadata)) { return result; }
    return execute_clip_with_fast_path_validated(request, metadata);
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
    result = execute_clip_with_fast_path_validated(request);
}

}  // namespace clipper2next::internal
