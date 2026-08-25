#include "offset/private/offset_group_processor.h"

#include "support/private/bulk_execution.h"

#include <algorithm>
#include <utility>
#include <vector>

namespace clipper2next::internal {
namespace {

struct offset_path_chunk final {
    std::size_t begin{};
    std::size_t end{};
};

[[nodiscard]] auto has_minimum_point_count(
    const offset_group& group,
    std::size_t minimum) -> bool {
    for (auto index = std::size_t{}; index < group.path_count(); ++index) {
        const auto path = group.path(index);
        if (path.size() >= minimum) {
            return true;
        }
        minimum -= path.size();
    }
    return minimum == 0U;
}

[[nodiscard]] auto has_joined_two_point_path(
    const offset_group& group) -> bool {
    if (group.end_type != EndType::Joined) {
        return false;
    }
    for (auto index = std::size_t{}; index < group.path_count(); ++index) {
        if (group.path(index).size() == 2U) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] auto build_offset_chunks(
    const offset_group& group,
    const std::size_t concurrency_limit)
    -> std::vector<offset_path_chunk> {
    const auto paths_per_full_chunk_set =
        group.path_count() / 4U +
        static_cast<std::size_t>(group.path_count() % 4U != 0U);
    const auto chunk_count = concurrency_limit >= paths_per_full_chunk_set
        ? group.path_count()
        : concurrency_limit * 4U;
    auto remaining_points = std::size_t{};
    for (auto index = std::size_t{}; index < group.path_count(); ++index) {
        remaining_points += group.path(index).size();
    }

    auto chunks = std::vector<offset_path_chunk>{};
    chunks.reserve(chunk_count);
    auto begin = std::size_t{};
    while (begin < group.path_count()) {
        const auto remaining_chunks = chunk_count - chunks.size();
        const auto target =
            (remaining_points + remaining_chunks - 1U) / remaining_chunks;
        auto end = begin;
        auto chunk_points = std::size_t{};
        const auto latest_end =
            group.path_count() - (remaining_chunks - 1U);
        while (end < latest_end &&
               (end == begin || chunk_points < target)) {
            chunk_points += group.path(end).size();
            ++end;
        }
        chunks.push_back({begin, end});
        begin = end;
        remaining_points -= chunk_points;
    }
    return chunks;
}

struct offset_bulk_context final {
    const offset_group* group{};
    const offset_group_execution_options* options{};
    const std::vector<offset_path_chunk>* chunks{};
    std::vector<path_set64>* results{};
    bulk_failure_state* failure{};
    double delta{};
};

void build_offset_chunk_range(
    void* const state,
    const std::size_t begin,
    const std::size_t end) noexcept {
    auto& context = *static_cast<offset_bulk_context*>(state);
    auto local_state = offset_state{};
    for (auto chunk_index = begin; chunk_index < end; ++chunk_index) {
        context.failure->invoke([&] {
            local_state.delta = context.delta;
            const auto chunk = (*context.chunks)[chunk_index];
            auto& output = (*context.results)[chunk_index];
            for (auto path_index = chunk.begin;
                 path_index < chunk.end;
                 ++path_index) {
                append_offset_path(
                    local_state,
                    *context.group,
                    context.group->path(path_index),
                    *context.options,
                    output);
            }
        });
    }
}

template <typename Output, typename Append>
void build_offset_group_paths_with_executor(
    offset_state& state,
    const offset_group& group,
    const offset_group_execution_options& options,
    const double delta,
    const sync_bulk_executor_ref executor,
    Output& output,
    Append append) {
    state.delta = delta;
    if (!executor.has_parallel_capability() ||
        executor.concurrency_limit() < offset_parallel_minimum_concurrency ||
        !is_offset_group_parallel_eligible(group)) {
        build_offset_group_paths(state, group, options, nullptr, output);
        return;
    }

    const auto chunks =
        build_offset_chunks(group, executor.concurrency_limit());
    auto results = std::vector<path_set64>(chunks.size());
    auto failure = bulk_failure_state{};
    auto context = offset_bulk_context{
        &group, &options, &chunks, &results, &failure, delta};
    const auto executor_error = executor.execute(
        chunks.size(), 1U,
        std::min(
            executor.concurrency_limit(),
            offset_parallel_maximum_concurrency),
        bulk_task_ref{&context, &build_offset_chunk_range});
    if (const auto error = failure.error(); error != clipper_error_code::ok) {
        raise_clipper_error(error);
    }
    if (executor_error != bulk_execution_error::none) {
        raise_clipper_error(executor_error_code(executor_error));
    }

    for (const auto& result : results) {
        for (const auto path : result) {
            append(output, path);
        }
    }
}

} // namespace

auto is_offset_group_parallel_eligible(const offset_group& group) -> bool {
    if (group.path_count() == 0U || has_joined_two_point_path(group) ||
        (group.end_type == EndType::Polygon &&
         !group.lowest_path_index.has_value())) {
        return false;
    }
    return group.path_count() >= offset_parallel_minimum_path_count &&
           has_minimum_point_count(
               group, offset_parallel_minimum_point_count);
}

auto build_offset_group_paths_parallel(
    offset_state& state,
    const offset_group& group,
    const offset_group_execution_options& options,
    const double delta,
    const sync_bulk_executor_ref executor,
    Paths64& output) -> void {
    build_offset_group_paths_with_executor(
        state, group, options, delta, executor, output,
        [](Paths64& destination, const std::span<const Point64> path) {
            destination.emplace_back(path.begin(), path.end());
        });
}

auto build_offset_group_paths_parallel(
    offset_state& state,
    const offset_group& group,
    const offset_group_execution_options& options,
    const double delta,
    const sync_bulk_executor_ref executor,
    path_set64& output) -> void {
    build_offset_group_paths_with_executor(
        state, group, options, delta, executor, output,
        [](path_set64& destination, const std::span<const Point64> path) {
            destination.append(
                path, geotypes::PathClosure::ClosedImplicit);
        });
}

} // namespace clipper2next::internal
