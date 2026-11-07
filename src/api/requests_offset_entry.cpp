#include "clipper2next/offset/operations.h"
#include "api/private/checked_execution.h"

#include "api/private/borrowed_offset_execution.h"
#include "offset/private/offset_request_validation.h"

namespace clipper2next {

auto offset_stage_checked(const borrowed_offset_request64& request)
    -> expected_borrowed_offset_stage_result64 {
    return offset_stage_checked(request, {});
}

auto offset_stage_checked(
    const borrowed_offset_request64& request,
    const sync_bulk_executor_ref executor)
    -> expected_borrowed_offset_stage_result64 {
    return internal::checked_call<borrowed_offset_stage_result64>(
        [&]() -> clipper_result<borrowed_offset_stage_result64> {
            const auto error = internal::validate_offset_request(request);
            if (error != clipper_error_code::ok) {
                return make_clipper_error<borrowed_offset_stage_result64>(error);
            }
            auto resources = internal::offset_engine_resource_context{};
            return internal::execute_borrowed_offset_stage(request, executor, resources);
        });
}

}  // namespace clipper2next
