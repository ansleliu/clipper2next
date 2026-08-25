#include "clipper2next/api/execution.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <vector>

namespace next = clipper2next;

namespace {

struct inline_executor final {
    std::size_t invocation_count{};
    std::size_t requested_concurrency{};
};

[[nodiscard]] auto execute_inline(
    void* const context,
    const std::size_t item_count,
    const std::size_t minimum_grain,
    const std::size_t requested_concurrency,
    const next::bulk_task_ref task) noexcept -> next::bulk_execution_error {
    auto& executor = *static_cast<inline_executor*>(context);
    executor.requested_concurrency = requested_concurrency;
    const auto grain = std::max<std::size_t>(minimum_grain, 1U);
    for (auto begin = std::size_t{}; begin < item_count; begin += grain) {
        ++executor.invocation_count;
        task(begin, std::min(item_count, begin + grain));
    }
    return next::bulk_execution_error::none;
}

} // namespace

TEST(Clipper2NextExecutionApiTests, EmptyExecutorHasOnlySerialCapability) {
    const auto executor = next::sync_bulk_executor_ref{};

    EXPECT_FALSE(executor.has_parallel_capability());
    EXPECT_EQ(executor.concurrency_limit(), 1U);
}

TEST(Clipper2NextExecutionApiTests, ExecutorVisitsEachItemExactlyOnceByChunks) {
    auto executor_state = inline_executor{};
    const auto executor = next::sync_bulk_executor_ref{
        &executor_state, 4U, &execute_inline};
    auto visits = std::vector<unsigned>(19U);
    const auto task = next::bulk_task_ref{
        &visits,
        [](void* const context,
           const std::size_t begin,
           const std::size_t end) noexcept {
            auto& counts = *static_cast<std::vector<unsigned>*>(context);
            for (auto index = begin; index < end; ++index) {
                ++counts[index];
            }
        }};

    const auto error = executor.execute(visits.size(), 4U, 3U, task);

    EXPECT_EQ(error, next::bulk_execution_error::none);
    EXPECT_EQ(executor_state.invocation_count, 5U);
    EXPECT_EQ(executor_state.requested_concurrency, 3U);
    EXPECT_EQ(visits, std::vector<unsigned>(visits.size(), 1U));
}

TEST(Clipper2NextExecutionApiTests, ZeroConcurrencyCannotAdvertiseParallelCapability) {
    auto executor_state = inline_executor{};
    const auto executor = next::sync_bulk_executor_ref{
        &executor_state, 0U, &execute_inline};

    EXPECT_FALSE(executor.has_parallel_capability());
    EXPECT_EQ(executor.concurrency_limit(), 1U);
}
