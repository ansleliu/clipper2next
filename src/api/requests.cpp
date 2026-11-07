#include "clipper2next/clip.h"
#include "api/private/checked_execution.h"

#include "clip/engine/private/engine_execution_context.h"
#include "clip/engine/private/engine_lifecycle.h"
#include "api/private/request_execution_helpers.h"
#include "clip/private/closed_clip_fast_path.h"
#include "clip/private/clip_execution_strategy.h"
#include "clip/private/clip_request_validation.h"
#include "clip/private/prepared_clip_runtime_data.h"
#include "clip/private/owning_clip_state.h"

#include "support/private/paths_rebuild.h"
#include "clip/engine/private/polygon_tree_rebuild_access.h"

namespace clipper2next {

namespace internal {

[[nodiscard]] auto try_execute_nested_rectangle_union_tree(const clip_request64& request,
                                                           clip_tree64_result& result) -> bool;

}  // namespace internal

namespace {

[[nodiscard]] auto is_empty_clip_operation(const clip_request64& request) -> bool {
    if (request.clip_type == ClipType::NoClip) { return true; }
    const auto has_no_points = [](const Paths64& paths) {
        for (const auto& path : paths) {
            if (!path.empty()) { return false; }
        }
        return true;
    };
    return has_no_points(request.subjects) && has_no_points(request.open_subjects) &&
           has_no_points(request.clips);
}

auto execute_clip_tree_into_impl(const clip_request64& request, PolyTree64& tree, Paths64& open)
    -> void {
    if (is_empty_clip_operation(request)) {
        polygon_tree_rebuild_access::materialize_empty(tree);
        open.clear();
        return;
    }

    auto lease = internal::owning_clip_state_lease{};
    auto& state = lease.state();
    const bool has_open_paths = !request.open_subjects.empty();

    internal::add_paths_to_state(state, request.subjects, PathType::Subject, false);
    internal::add_paths_to_state(state, request.open_subjects, PathType::Subject, true);
    internal::add_paths_to_state(state, request.clips, PathType::Clip, false);

    internal::engine_execution_context context{state};
    internal::run_owning_clip(request, context, has_open_paths, true);
    internal::build_request_clip_tree(context, request.options, tree, open);
}

}  // namespace

namespace internal {

namespace {

auto execute_clip_paths_validated(const clip_request64& request,
                                  Paths64& closed,
                                  Paths64& open) -> void {
    if (is_empty_clip_operation(request)) {
        closed.clear();
        open.clear();
        return;
    }
    auto lease = internal::owning_clip_state_lease{};
    auto& state = lease.state();
    const bool has_open_paths = !request.open_subjects.empty();

    add_paths_to_state(state, request.subjects, PathType::Subject, false);
    add_paths_to_state(state, request.open_subjects, PathType::Subject, true);
    add_paths_to_state(state, request.clips, PathType::Clip, false);

    engine_execution_context context{state};
    run_owning_clip(request, context, has_open_paths, false);
    const bool result_provably_empty =
        request.subjects.empty() &&
        (request.clip_type == ClipType::Intersection || request.clip_type == ClipType::Difference);
    const bool collect_closed_paths = !result_provably_empty;
    build_request_clip_paths(
        context, request.options, closed, open, collect_closed_paths, has_open_paths);
}

}  // namespace

auto execute_clip_validated(const clip_request64& request) -> paths64_result {
    paths64_result result;
    execute_clip_paths_validated(
        request, result.closed, result.open);
    return result;
}

auto execute_prepared_clip_runtime(const clip_request64& clip_request,
                                   const auto& runtime_data,
                                   paths64_result& result) -> void {
    if (runtime_data && runtime_data->cached_result.has_value()) {
        const auto& cached = *runtime_data->cached_result;
        paths_rebuild<int64_t> closed{result.closed}, open{result.open};
        closed.prepare(cached.closed.size(), [&](auto i) { return cached.closed[i].size(); });
        open.prepare(cached.open.size(), [&](auto i) { return cached.open[i].size(); });
        closed.begin();
        open.begin();
        for (std::size_t i = 0; i < cached.closed.size(); ++i) {
            result.closed[i] = cached.closed[i];
        }
        for (std::size_t i = 0; i < cached.open.size(); ++i) { result.open[i] = cached.open[i]; }
        closed.finish();
        open.finish();
        return;
    }

    if (is_empty_clip_operation(clip_request)) {
        result.closed.clear();
        result.open.clear();
        return;
    }
    auto lease = internal::owning_clip_state_lease{};
    auto& state = lease.state();
    bool has_open_paths = !clip_request.open_subjects.empty();

    if (runtime_data) {
        state.precomputed_scanline_heap_ = runtime_data->scanline_heap;
        append_reuseable_data(state, runtime_data->reusable_state, has_open_paths);
        state.minima_list_sorted_ = runtime_data->minima_sorted;
    } else {
        add_paths_to_state(state, clip_request.subjects, PathType::Subject, false);
        add_paths_to_state(state, clip_request.open_subjects, PathType::Subject, true);
        add_paths_to_state(state, clip_request.clips, PathType::Clip, false);
    }

    engine_execution_context context{state};
    run_owning_clip(clip_request, context, has_open_paths, false);
    const bool result_provably_empty =
        clip_request.subjects.empty() && (clip_request.clip_type == ClipType::Intersection ||
                                          clip_request.clip_type == ClipType::Difference);
    const bool collect_closed_paths = !result_provably_empty;
    build_request_clip_paths(context,
                             clip_request.options,
                             result.closed,
                             result.open,
                             collect_closed_paths,
                             has_open_paths);
}

auto execute_clip_into_validated(const clip_request64& request, paths64_result& result) -> void {
    execute_clip_paths_validated(
        request, result.closed, result.open);
}

auto execute_clip_paths_into_validated(const clip_request64& request,
                                       Paths64& closed,
                                       Paths64& open) -> void {
    execute_clip_paths_validated(
        request, closed, open);
}

auto execute_clip_tree_into_validated(const clip_request64& request,
                                      PolyTree64& tree,
                                      Paths64& open) -> void {
    execute_clip_tree_into_impl(request, tree, open);
}

}  // namespace internal

auto clip(const clip_request64& request) -> paths64_result {
    return internal::execute_clip_with_fast_path_validated(request);
}

auto clip(const prepared_clip_request64& request) -> paths64_result {
    const auto& clip_request = request.request();
    paths64_result result;
    if (internal::try_execute_closed_clip_fast_path(request, result)) { return result; }
    internal::execute_prepared_clip_runtime(clip_request, request.runtime_data_, result);
    return result;
}

auto clip_checked(const clip_request64& request) -> expected_paths64_result {
    return internal::checked_call<paths64_result>([&]() -> clipper_result<paths64_result> {
        if (const auto error = internal::validate_clip_request(request);
            error != clipper_error_code::ok) {
            return make_clipper_error<paths64_result>(error);
        }
        return internal::execute_clip_with_fast_path_validated(request);
    });
}

auto clip_checked(const prepared_clip_request64& request) -> expected_paths64_result {
    return internal::checked_call<paths64_result>(
        [&]() -> clipper_result<paths64_result> { return clip(request); });
}

auto clip_into(const clip_request64& request, paths64_result& result) -> void {
    internal::execute_clip_into_with_fast_path(request, result);
}

auto clip_into(const prepared_clip_request64& request, paths64_result& result) -> void {
    if (internal::try_execute_closed_clip_fast_path(request, result)) { return; }
    internal::execute_prepared_clip_runtime(request.request(), request.runtime_data_, result);
}

auto clip_tree(const clip_request64& request) -> clip_tree64_result {
    clip_tree64_result result;
    if (internal::try_execute_nested_rectangle_union_tree(request, result)) { return result; }
    execute_clip_tree_into_impl(request, result.tree, result.open);
    return result;
}

auto clip_tree_checked(const clip_request64& request) -> expected_clip_tree64_result {
    return internal::checked_call<clip_tree64_result>([&]() -> clipper_result<clip_tree64_result> {
        clip_tree64_result result;
        if (const auto error = internal::validate_clip_request(request);
            error != clipper_error_code::ok) {
            return make_clipper_error<clip_tree64_result>(error);
        }
        if (internal::try_execute_nested_rectangle_union_tree(request, result)) { return result; }
        execute_clip_tree_into_impl(request, result.tree, result.open);
        return result;
    });
}

auto clip_tree_into(const clip_request64& request, clip_tree64_result& result) -> void {
    if (internal::try_execute_nested_rectangle_union_tree(request, result)) { return; }
    execute_clip_tree_into_impl(request, result.tree, result.open);
}

}  // namespace clipper2next
