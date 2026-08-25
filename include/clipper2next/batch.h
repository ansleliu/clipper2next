#pragma once

#include "clipper2next/api/export.h"
#include "clipper2next/api/execution.h"
#include "clipper2next/clip/request.h"

#include <span>
#include <vector>

namespace clipper2next {

[[nodiscard]] CLIPPER2NEXT_API auto clip_batch(std::span<const clip_request64> requests)
    -> std::vector<paths64_result>;
[[nodiscard]] CLIPPER2NEXT_API auto clip_batch(
    std::span<const prepared_clip_request64> requests)
    -> std::vector<paths64_result>;
using expected_batch_results64 =
    clipper_result<std::vector<paths64_result>>;
[[nodiscard]] CLIPPER2NEXT_API auto clip_batch_checked(
    std::span<const clip_request64> requests,
    sync_bulk_executor_ref executor)
    -> expected_batch_results64;
[[nodiscard]] CLIPPER2NEXT_API auto clip_batch_checked(
    std::span<const prepared_clip_request64> requests,
    sync_bulk_executor_ref executor)
    -> expected_batch_results64;

}  // namespace clipper2next
