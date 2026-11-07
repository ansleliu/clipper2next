#include "offset/private/offset_group_processor.h"

#include "support/private/bulk_execution.h"
#include "offset/private/offset_parallel_results.h"

#include <algorithm>
#include <utility>
#include <vector>

namespace clipper2next::internal {
namespace {



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

[[nodiscard]] auto build_offset_chunks(
    const offset_group& group,
    const std::size_t concurrency_limit, scoped_workspace_charge& charge)
    -> std::vector<offset_path_chunk> {
    const auto paths_per_full_chunk_set =
        group.path_count() / 4U +
        static_cast<std::size_t>(group.path_count() % 4U != 0U);
    const auto chunk_count = concurrency_limit >= paths_per_full_chunk_set
        ? group.path_count()
        : concurrency_limit * 4U;
    auto remaining_points = std::size_t{};
    for (auto index = std::size_t{}; index < group.path_count(); ++index) {
        remaining_points = checked_size_add(remaining_points, group.path(index).size());
    }

    auto chunks = std::vector<offset_path_chunk>{};
    chunks.reserve(chunk_count);
    auto begin = std::size_t{};
    while (begin < group.path_count()) {
        const auto remaining_chunks = chunk_count - chunks.size();
        const auto target =
            remaining_points / remaining_chunks + static_cast<std::size_t>(remaining_points % remaining_chunks != 0U);
        auto end = begin;
        auto chunk_points = std::size_t{};
        const auto latest_end =
            group.path_count() - (remaining_chunks - 1U);
        while (end < latest_end &&
               (end == begin || chunk_points < target)) {
            chunk_points += group.path(end).size();
            ++end;
        }
        auto growth = scoped_workspace_growth{charge, chunks.size() + 1U, sizeof(offset_path_chunk)};
        chunks.push_back({begin, end});
        growth.commit();
        begin = end;
        remaining_points -= chunk_points;
    }
    return chunks;
}



struct offset_bulk_context final {
    const offset_group* group{};
    const offset_group_execution_options* options{};
    const std::vector<offset_path_chunk>* chunks{};
    offset_parallel_results* results{};
    bulk_failure_state* failure{};
    double delta{};
    bool budgeted{};
};

void build_offset_chunk_range(
    void* const state,
    const std::size_t begin,
    const std::size_t end) noexcept {
    auto& context = *static_cast<offset_bulk_context*>(state);
    context.failure->invoke([&] {
        auto local_state = offset_state{};
        for (auto chunk_index = begin; chunk_index < end; ++chunk_index) {
            const auto chunk = (*context.chunks)[chunk_index];
            auto& result = (*context.results)[chunk_index];
            auto& output = result.paths;
            auto scope = offset_resource_scope{local_state, context.budgeted ? &result.resources : nullptr,
                context.budgeted ? &result.output_charge : nullptr};
            for (auto path_index = chunk.begin;
                 path_index < chunk.end;
                 ++path_index) {
                append_offset_path(
                    local_state,
                    *context.group,
                    context.group->path(path_index),
                    *context.options,
                    context.delta,
                    output);
            }
        }
    });
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
    if (!executor.has_parallel_capability() ||
        executor.concurrency_limit() < offset_parallel_minimum_concurrency ||
        !is_offset_group_parallel_eligible(group)) {
        build_offset_group_paths(state, group, options, delta, nullptr, output);
        return;
    }

    // The previous serial group has finished. Workers own all scratch in
    // this phase; retain only the parent raw output, bindings and capacity.
    state.clear_scratch();
    const auto effective_concurrency = std::min(
        executor.concurrency_limit(),
        offset_parallel_maximum_concurrency);
    auto* resources = state.path_out_charge.context();
    auto chunks_charge = scoped_workspace_charge{resources, workspace_category::staging};
    const auto chunks =
        build_offset_chunks(group, effective_concurrency, chunks_charge);
    auto results = offset_parallel_results{resources, group, options, delta, chunks, effective_concurrency};
    auto failure = bulk_failure_state{};
    auto context = offset_bulk_context{
        &group, &options, &chunks, &results, &failure, delta, resources != nullptr};
    const auto executor_error = executor.execute(
        chunks.size(), 1U,
        effective_concurrency,
        bulk_task_ref{&context, &build_offset_chunk_range});
    results.observe_quiescence();
    if (const auto error = failure.error(); error != clipper_error_code::ok) {
        raise_clipper_error(error);
    }
    if (executor_error != bulk_execution_error::none) {
        raise_clipper_error(executor_error_code(executor_error));
    }

    results.prepare_merge();
    for (std::size_t index = 0; index < results.size(); ++index) {
        const auto& result = results[index];
        for (const auto path : result.paths) {
            constexpr auto descriptor_bytes = std::is_same_v<Output, path_set64>
                ? sizeof(geotypes::PathDescriptor) : sizeof(Path64);
            auto growth = state.reserve_output(path.size(), descriptor_bytes);
            append(output, path);
            growth.commit();
        }
        results.consume(index);
    }
}

} // namespace

auto is_offset_group_parallel_eligible(const offset_group& group) -> bool {
    if (group.path_count() == 0U ||
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
