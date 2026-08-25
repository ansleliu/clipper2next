#pragma once

#include "clip/engine/private/engine_state.h"
#include "clipper2next/api/result.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace clipper2next {

// Shared by every execution of a prepared request, including concurrent batch
// executions on multiple threads. Correctness relies on the invariant that the
// engine treats Vertex storage and the local-minima list as strictly read-only
// during execute(): each execution copies the minima list into its own state
// but the Vertex nodes those minima reference live here and are shared.
// Any future engine change that mutates Vertex (points, links, or flags) during
// execution breaks prepared/batch concurrency and must copy vertices instead.
struct prepared_clip_request64::runtime_data final {
    internal::reuseable_data_state reusable_state{};
    bool minima_sorted = false;
    std::vector<int64_t> scanline_heap{};
    std::optional<paths64_result> cached_result{};
};

}  // namespace clipper2next
