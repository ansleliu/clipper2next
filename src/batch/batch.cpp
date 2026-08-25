#include "clipper2next/batch.h"
#include "clipper2next/clip.h"

#include "clip/private/clip_execution_strategy.h"

#include <span>
#include <vector>

namespace {

[[nodiscard]] auto clip_batch_item(
    const clipper2next::clip_request64& request)
    -> clipper2next::paths64_result {
    return clipper2next::internal::execute_clip_with_fast_path(request);
}

[[nodiscard]] auto clip_batch_item(
    const clipper2next::prepared_clip_request64& request)
    -> clipper2next::paths64_result {
    return clipper2next::clip(request);
}

template <typename Request>
[[nodiscard]] auto clip_batch_sequential(
    const std::span<const Request> requests)
    -> std::vector<clipper2next::paths64_result> {
    auto results = std::vector<clipper2next::paths64_result>{};
    results.reserve(requests.size());
    for (const auto& request : requests) {
        results.push_back(clip_batch_item(request));
    }
    return results;
}

} // namespace

namespace clipper2next {

auto clip_batch(const std::span<const clip_request64> requests)
    -> std::vector<paths64_result> {
    return clip_batch_sequential(requests);
}

auto clip_batch(const std::span<const prepared_clip_request64> requests)
    -> std::vector<paths64_result> {
    return clip_batch_sequential(requests);
}

} // namespace clipper2next
