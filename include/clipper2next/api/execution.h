// SPDX-License-Identifier: BSL-1.0

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace clipper2next {

enum class bulk_execution_error : std::uint8_t {
    none,
    allocation_failure,
    scheduler_failure,
};

class bulk_task_ref final {
public:
    // Invocations receive non-overlapping half-open ranges. The executor must
    // cover [0, item_count) exactly once before returning.
    using invoke_function = void (*)(
        void*, std::size_t, std::size_t) noexcept;

    constexpr bulk_task_ref() noexcept = default;

    constexpr bulk_task_ref(
        void* const state,
        const invoke_function invoke) noexcept
        : state_{state}, invoke_{invoke} {}

    constexpr void operator()(
        const std::size_t begin,
        const std::size_t end) const noexcept {
        if (invoke_ != nullptr && begin < end) {
            invoke_(state_, begin, end);
        }
    }

private:
    void* state_{};
    invoke_function invoke_{};
};

class sync_bulk_executor_ref final {
public:
    // Executes bulk_task_ref synchronously. The callback must not retain the
    // executor state, task, or task state, and requested_concurrency is a hard
    // upper bound for this call.
    using execute_function = bulk_execution_error (*)(
        void*, std::size_t, std::size_t, std::size_t,
        bulk_task_ref) noexcept;

    // This is a borrowed capability. Its state must outlive every execute()
    // call and is never retained by clipper2next.
    constexpr sync_bulk_executor_ref() noexcept = default;

    constexpr sync_bulk_executor_ref(
        void* const state,
        const std::size_t concurrency_limit,
        const execute_function execute) noexcept
        : state_{state},
          concurrencyLimit_{std::max<std::size_t>(concurrency_limit, 1U)},
          execute_{concurrency_limit > 1U ? execute : nullptr} {}

    [[nodiscard]] constexpr auto has_parallel_capability() const noexcept
        -> bool {
        return execute_ != nullptr;
    }

    [[nodiscard]] constexpr auto concurrency_limit() const noexcept
        -> std::size_t {
        return concurrencyLimit_;
    }

    [[nodiscard]] constexpr auto execute(
        const std::size_t item_count,
        const std::size_t minimum_grain,
        const std::size_t requested_concurrency,
        const bulk_task_ref task) const noexcept -> bulk_execution_error {
        if (item_count == 0U) {
            return bulk_execution_error::none;
        }
        if (!has_parallel_capability()) {
            return bulk_execution_error::scheduler_failure;
        }
        return execute_(
            state_, item_count,
            std::max<std::size_t>(minimum_grain, 1U),
            std::clamp<std::size_t>(
                requested_concurrency, 1U, concurrencyLimit_),
            task);
    }

private:
    void* state_{};
    std::size_t concurrencyLimit_{1U};
    execute_function execute_{};
};

} // namespace clipper2next
