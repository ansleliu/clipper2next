#pragma once

#include "clipper2next/api/execution.h"
#include "support/private/engine_resource_plan.h"
#include "clipper2next/offset/borrowed.h"

namespace clipper2next::internal {

[[nodiscard]] auto execute_borrowed_offset_stage(
    const borrowed_offset_request64& request,
    sync_bulk_executor_ref executor,
    offset_engine_resource_context& resources)
    -> expected_borrowed_offset_stage_result64;

}  // namespace clipper2next::internal
