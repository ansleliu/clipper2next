#pragma once

#include "clipper2next/clip/request.h"

namespace clipper2next::internal {
auto execute_clip_into_validated(const clip_request64&, paths64_result&) -> void;
auto execute_clip_paths_into_validated(const clip_request64&, Paths64&, Paths64&) -> void;
auto execute_clip_tree_into_validated(const clip_request64&, PolyTree64&, Paths64&) -> void;

[[nodiscard]] auto execute_clip_validated(const clip_request64& request) -> paths64_result;

[[nodiscard]] auto execute_clip_with_fast_path_validated(const clip_request64& request)
    -> paths64_result;
[[nodiscard]] auto execute_clip_with_fast_path_validated(
    const clip_request64& request,
    const clip_request_metadata64& metadata) -> paths64_result;

auto execute_clip_into_with_fast_path(const clip_request64& request, paths64_result& result)
    -> void;

// Frees this thread's reusable clip engine state (see release_thread_caches).
auto release_clip_thread_state() noexcept -> void;

}  // namespace clipper2next::internal
