#include <benchmark/benchmark.h>

#include "clipper2next/api/execution.h"
#include "clipper2next/offset.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <new>
#include <ranges>
#include <thread>
#include <vector>

namespace {

struct thread_executor final {
    std::size_t worker_count{};
};

[[nodiscard]] auto execute_chunks(
    void* const state,
    const std::size_t item_count,
    const std::size_t minimum_grain,
    const std::size_t requested_concurrency,
    const clipper2next::bulk_task_ref task) noexcept
    -> clipper2next::bulk_execution_error {
    const auto worker_count = std::min(
        static_cast<thread_executor*>(state)->worker_count,
        requested_concurrency);
    const auto grain = std::max<std::size_t>(minimum_grain, 1U);
    auto next = std::atomic_size_t{};
    const auto worker = [&] {
        while (true) {
            const auto begin = next.fetch_add(
                grain, std::memory_order_relaxed);
            if (begin >= item_count) {
                return;
            }
            task(begin, std::min(item_count, begin + grain));
        }
    };
    try {
        auto threads = std::vector<std::jthread>{};
        threads.reserve(worker_count - 1U);
        for (auto index = std::size_t{1U}; index < worker_count; ++index) {
            threads.emplace_back(worker);
        }
        worker();
    } catch (const std::bad_alloc&) {
        return clipper2next::bulk_execution_error::allocation_failure;
    } catch (...) {
        return clipper2next::bulk_execution_error::scheduler_failure;
    }
    return clipper2next::bulk_execution_error::none;
}

[[nodiscard]] auto dense_rectangle(
    const std::int64_t left,
    const std::int64_t top) -> clipper2next::Path64 {
    constexpr auto edge = std::int64_t{256};
    auto path = clipper2next::Path64{};
    path.reserve(1'024U);
    for (auto value = std::int64_t{}; value <= edge; ++value) {
        path.push_back({left + value, top});
    }
    for (auto value = std::int64_t{1}; value <= edge; ++value) {
        path.push_back({left + edge, top + value});
    }
    for (auto value = edge - 1; value >= 0; --value) {
        path.push_back({left + value, top + edge});
    }
    for (auto value = edge - 1; value > 0; --value) {
        path.push_back({left, top + value});
    }
    return path;
}

[[nodiscard]] auto source_paths() -> clipper2next::Paths64 {
    auto paths = clipper2next::Paths64{};
    paths.reserve(512U);
    for (auto index = std::int64_t{}; index < 512; ++index) {
        paths.push_back(dense_rectangle(index * 1'000, 0));
    }
    return paths;
}

[[nodiscard]] auto request_for(
    const clipper2next::Paths64& paths)
    -> clipper2next::borrowed_offset_request64 {
    auto request = clipper2next::borrowed_offset_request64{};
    request.paths = clipper2next::borrow_paths64(paths);
    request.delta = 5.0;
    request.join_type = clipper2next::JoinType::Miter;
    request.end_type = clipper2next::EndType::Polygon;
    return request;
}

[[nodiscard]] auto same_path_set(
    const clipper2next::path_set64& first,
    const clipper2next::path_set64& second) -> bool {
    if (!std::ranges::equal(first.points(), second.points()) ||
        first.descriptors().size() != second.descriptors().size()) {
        return false;
    }
    for (auto index = std::size_t{};
         index < first.descriptors().size(); ++index) {
        const auto& left = first.descriptors()[index];
        const auto& right = second.descriptors()[index];
        if (left.pointOffset != right.pointOffset ||
            left.pointCount != right.pointCount ||
            left.closure != right.closure) {
            return false;
        }
    }
    return true;
}

void BM_offset_executor_serial(benchmark::State& state) {
    const auto paths = source_paths();
    const auto request = request_for(paths);
    for (auto iteration : state) {
        static_cast<void>(iteration);
        auto result = clipper2next::offset_stage_checked(request);
        if (!result) {
            state.SkipWithError("serial offset failed");
            break;
        }
        benchmark::DoNotOptimize(result->paths.points().data());
    }
}

void BM_offset_executor_concurrent(benchmark::State& state) {
    const auto paths = source_paths();
    const auto request = request_for(paths);
    auto executor_state = thread_executor{
        static_cast<std::size_t>(state.range(0))};
    const auto executor = clipper2next::sync_bulk_executor_ref{
        &executor_state, executor_state.worker_count, &execute_chunks};
    const auto serial = clipper2next::offset_stage_checked(request);
    const auto check = clipper2next::offset_stage_checked(request, executor);
    if (!serial || !check || !same_path_set(serial->paths, check->paths)) {
        state.SkipWithError("executor offset differs from serial output");
        return;
    }
    for (auto iteration : state) {
        static_cast<void>(iteration);
        auto result = clipper2next::offset_stage_checked(request, executor);
        if (!result) {
            state.SkipWithError("executor offset failed");
            break;
        }
        benchmark::DoNotOptimize(result->paths.points().data());
    }
}

BENCHMARK(BM_offset_executor_serial);
BENCHMARK(BM_offset_executor_concurrent)->Arg(2)->Arg(4)->Arg(8)->Arg(16);

} // namespace
