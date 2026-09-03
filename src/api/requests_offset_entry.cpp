#include "clipper2next/offset/operations.h"

#include "api/private/borrowed_offset_execution.h"

#include <new>
#include <stdexcept>

namespace clipper2next {

auto offset_stage_checked(const borrowed_offset_request64& request)
    -> expected_borrowed_offset_stage_result64 {
    return offset_stage_checked(request, {});
}

auto offset_stage_checked(
    const borrowed_offset_request64& request,
    const sync_bulk_executor_ref executor)
    -> expected_borrowed_offset_stage_result64 {
    try {
        return internal::execute_borrowed_offset_stage(request, executor);
    } catch (const std::bad_alloc&) {
        return make_clipper_error<borrowed_offset_stage_result64>(
            clipper_error_code::allocation_failure);
    } catch (const std::length_error&) {
        return make_clipper_error<borrowed_offset_stage_result64>(
            clipper_error_code::resource_limit);
    } catch (const clipper_error& error) {
        return make_clipper_error<borrowed_offset_stage_result64>(error.code());
    } catch (...) {
        return make_clipper_error<borrowed_offset_stage_result64>(
            clipper_error_code::internal_error);
    }
}

}  // namespace clipper2next
