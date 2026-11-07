#include "clipper2next/batch.h"
#include "clipper2next/clip.h"

#include "clip/private/clip_execution_strategy.h"

#include <span>
#include <vector>

namespace {

auto clip_batch_item_into(const clipper2next::clip_request64& request,
                          clipper2next::paths64_result& result) -> void {
    clipper2next::internal::execute_clip_into_with_fast_path(request, result);
}

auto clip_batch_item_into(const clipper2next::prepared_clip_request64& request,
                          clipper2next::paths64_result& result) -> void {
    clipper2next::clip_into(request, result);
}

template <typename Request>
[[nodiscard]] auto clip_batch_sequential(
    const std::span<const Request> requests)
    -> std::vector<clipper2next::paths64_result> {
    auto results = std::vector<clipper2next::paths64_result>{};
    results.reserve(requests.size());
    for (const auto& request : requests) {
        clip_batch_item_into(request, results.emplace_back());
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
