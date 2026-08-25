// SPDX-License-Identifier: BSL-1.0

#pragma once

#include "clipper2next/api/export.h"
#include "clipper2next/api/execution.h"
#include "clipper2next/offset/borrowed.h"
#include "clipper2next/offset/request.h"

namespace clipper2next {

[[nodiscard]] CLIPPER2NEXT_API auto offset(const offset_request64& request)
    -> paths64_result;
[[nodiscard]] CLIPPER2NEXT_API auto offset_checked(const offset_request64& request)
    -> expected_paths64_result;
[[nodiscard]] CLIPPER2NEXT_API auto offset_stage_checked(
    const borrowed_offset_request64& request)
    -> expected_borrowed_offset_stage_result64;
[[nodiscard]] CLIPPER2NEXT_API auto offset_stage_checked(
    const borrowed_offset_request64& request,
    sync_bulk_executor_ref executor)
    -> expected_borrowed_offset_stage_result64;
CLIPPER2NEXT_API auto offset_into(
    const offset_request64& request, paths64_result& result) -> void;

}  // namespace clipper2next
