#include "clipper2next/batch.h"
#include "clipper2next/clip.h"
#include "batch/private/batch_clip_executor.h"
#include "clip/private/clip_execution_strategy.h"
#include "clip/private/clip_request_metadata.h"
#include "clip/private/clip_request_validation.h"
#include "support/private/bulk_execution.h"
#include <algorithm>
#include <cstddef>
#include <new>
#include <optional>
#include <span>
#include <stdexcept>
#include <utility>
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
[[nodiscard]] auto clip_batch_item(
    const clipper2next::clip_request64& request,
    const clipper2next::clip_request_metadata64& metadata)
    -> clipper2next::paths64_result {
    return clipper2next::internal::execute_clip_with_fast_path(
        request, metadata);
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
[[nodiscard]] auto build_metadata(
    const std::span<const clipper2next::clip_request64> requests)
    -> std::vector<clipper2next::clip_request_metadata64> {
    auto metadata = std::vector<clipper2next::clip_request_metadata64>{};
    metadata.reserve(requests.size());
    for (const auto& request : requests) {
        if (!clipper2next::internal::clip_request_in_range(request)) {
            metadata.emplace_back();
        } else {
            metadata.push_back(
                clipper2next::internal::build_clip_request_metadata(request));
        }
    }
    return metadata;
}
[[nodiscard]] auto build_profiles(
    const std::span<const clipper2next::clip_request64> requests,
    const std::span<const clipper2next::clip_request_metadata64> metadata)
    -> std::vector<clipper2next::internal::clip_batch_work_profile> {
    auto profiles =
        std::vector<clipper2next::internal::clip_batch_work_profile>{};
    profiles.reserve(requests.size());
    for (auto index = std::size_t{}; index < requests.size(); ++index) {
        profiles.push_back(
            clipper2next::internal::build_clip_batch_work_profile(
                index, requests[index], metadata[index]));
    }
    return profiles;
}
[[nodiscard]] auto build_profiles(
    const std::span<const clipper2next::prepared_clip_request64> requests)
    -> std::vector<clipper2next::internal::clip_batch_work_profile> {
    auto profiles =
        std::vector<clipper2next::internal::clip_batch_work_profile>{};
    profiles.reserve(requests.size());
    for (auto index = std::size_t{}; index < requests.size(); ++index) {
        profiles.push_back(
            clipper2next::internal::build_clip_batch_work_profile(
                index, requests[index].request(), requests[index].metadata()));
    }
    return profiles;
}

template <typename Operation>
struct batch_context final {
    Operation* operation{};
    std::vector<std::optional<clipper2next::paths64_result>>* slots{};
    clipper2next::internal::bulk_failure_state* failure{};
    std::span<const std::size_t> order{};
};

template <typename Operation>
void execute_chunks(
    void* const state,
    const std::size_t begin,
    const std::size_t end) noexcept {
    auto& context = *static_cast<batch_context<Operation>*>(state);
    for (auto index = begin; index < end; ++index) {
        const auto input_index = context.order.empty()
            ? index
            : context.order[index];
        context.failure->invoke([&] {
            (*context.slots)[input_index].emplace(
                (*context.operation)(input_index));
        });
    }
}

template <typename Operation>
[[nodiscard]] auto execute_batch(
    const clipper2next::sync_bulk_executor_ref executor,
    const std::size_t count,
    Operation operation,
    const std::span<const std::size_t> order = {})
    -> clipper2next::expected_batch_results64 {
    auto slots =
        std::vector<std::optional<clipper2next::paths64_result>>(count);
    auto failure = clipper2next::internal::bulk_failure_state{};
    auto context = batch_context<Operation>{
        &operation, &slots, &failure, order};
    const auto executor_error = executor.execute(
        count,
        clipper2next::internal::clip_batch_parallel_grain_size(),
        std::min(
            executor.concurrency_limit(),
            clipper2next::internal::clip_batch_parallel_maximum_concurrency()),
        clipper2next::bulk_task_ref{
            &context, &execute_chunks<Operation>});
    if (const auto error = failure.error();
        error != clipper2next::clipper_error_code::ok) {
        return clipper2next::make_clipper_error<
            std::vector<clipper2next::paths64_result>>(error);
    }
    if (executor_error != clipper2next::bulk_execution_error::none) {
        return clipper2next::make_clipper_error<
            std::vector<clipper2next::paths64_result>>(
            clipper2next::internal::executor_error_code(executor_error));
    }

    auto results = std::vector<clipper2next::paths64_result>{};
    results.reserve(count);
    for (auto& slot : slots) {
        if (!slot.has_value()) {
            return clipper2next::make_clipper_error<
                std::vector<clipper2next::paths64_result>>(
                clipper2next::clipper_error_code::executor_failure);
        }
        results.push_back(std::move(*slot));
    }
    return results;
}

template <typename Operation>
[[nodiscard]] auto checked_call(Operation operation)
    -> clipper2next::expected_batch_results64 {
    try {
        return operation();
    } catch (const std::bad_alloc&) {
        return clipper2next::make_clipper_error<
            std::vector<clipper2next::paths64_result>>(
            clipper2next::clipper_error_code::allocation_failure);
    } catch (const std::length_error&) {
        return clipper2next::make_clipper_error<
            std::vector<clipper2next::paths64_result>>(
            clipper2next::clipper_error_code::resource_limit);
    } catch (const clipper2next::clipper_error& error) {
        return clipper2next::make_clipper_error<
            std::vector<clipper2next::paths64_result>>(error.code());
    } catch (...) {
        return clipper2next::make_clipper_error<
            std::vector<clipper2next::paths64_result>>(
            clipper2next::clipper_error_code::internal_error);
    }
}

} // namespace

namespace clipper2next {

auto clip_batch_checked(
    const std::span<const clip_request64> requests,
    const sync_bulk_executor_ref executor)
    -> expected_batch_results64 {
    return checked_call([&]() -> expected_batch_results64 {
        if (!executor.has_parallel_capability() ||
            requests.size() < internal::clip_batch_parallel_threshold() ||
            !internal::clip_batch_requests_are_parallel_safe(requests)) {
            return clip_batch_sequential(requests);
        }
        if (internal::clip_batch_requests_have_uniform_shape(requests)) {
            return execute_batch(
                executor, requests.size(),
                [&](const std::size_t index) {
                    return clip_batch_item(requests[index]);
                });
        }
        const auto metadata = build_metadata(requests);
        const auto profiles = build_profiles(requests, metadata);
        if (internal::clip_batch_work_profiles_are_uniform(profiles)) {
            return execute_batch(
                executor, requests.size(),
                [&](const std::size_t index) {
                    return clip_batch_item(requests[index], metadata[index]);
                });
        }
        const auto order = internal::build_clip_batch_work_order(profiles);
        return execute_batch(
            executor, order.size(),
            [&](const std::size_t input_index) {
                return clip_batch_item(
                    requests[input_index], metadata[input_index]);
            }, order);
    });
}

auto clip_batch_checked(
    const std::span<const prepared_clip_request64> requests,
    const sync_bulk_executor_ref executor)
    -> expected_batch_results64 {
    return checked_call([&]() -> expected_batch_results64 {
        if (!executor.has_parallel_capability() ||
            requests.size() < internal::clip_batch_parallel_threshold() ||
            !internal::clip_batch_requests_are_parallel_safe(requests)) {
            return clip_batch_sequential(requests);
        }
        const auto profiles = build_profiles(requests);
        if (internal::clip_batch_work_profiles_are_uniform(profiles)) {
            return execute_batch(
                executor, requests.size(),
                [&](const std::size_t index) {
                    return clip_batch_item(requests[index]);
                });
        }
        const auto order = internal::build_clip_batch_work_order(profiles);
        return execute_batch(
            executor, order.size(),
            [&](const std::size_t input_index) {
                return clip_batch_item(requests[input_index]);
            }, order);
    });
}

} // namespace clipper2next
