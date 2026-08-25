// SPDX-License-Identifier: BSL-1.0

#pragma once

#include "clipper2next/api/memory.h"
#include "clipper2next/clip/request.h"
#include "clipper2next/clip/topology.h"

namespace clipper2next {

[[nodiscard]] CLIPPER2NEXT_API auto prepare_clip_request(clip_request64 request)
    -> prepared_clip_request64;
[[nodiscard]] CLIPPER2NEXT_API auto clip(const clip_request64& request) -> paths64_result;
[[nodiscard]] CLIPPER2NEXT_API auto clip(const prepared_clip_request64& request)
    -> paths64_result;
[[nodiscard]] CLIPPER2NEXT_API auto clip_checked(const clip_request64& request)
    -> expected_paths64_result;
[[nodiscard]] CLIPPER2NEXT_API auto clip_checked(const prepared_clip_request64& request)
    -> expected_paths64_result;
CLIPPER2NEXT_API auto clip_into(const clip_request64& request, paths64_result& result) -> void;
CLIPPER2NEXT_API auto clip_into(const prepared_clip_request64& request,
                               paths64_result& result) -> void;
[[nodiscard]] CLIPPER2NEXT_API auto clip_tree(const clip_request64& request)
    -> clip_tree64_result;
[[nodiscard]] CLIPPER2NEXT_API auto clip_tree_checked(const clip_request64& request)
    -> expected_clip_tree64_result;
CLIPPER2NEXT_API auto clip_tree_into(const clip_request64& request,
                                    clip_tree64_result& result) -> void;

}  // namespace clipper2next
