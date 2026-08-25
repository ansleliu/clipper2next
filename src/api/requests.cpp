#include "clipper2next/clip.h"
#include "clipper2next/geometry.h"

#include "clip/engine/private/engine_execution_context.h"
#include "clip/engine/private/engine_lifecycle.h"
#include "clip/engine/private/engine_scanbeam_processor.h"
#include "clip/engine/private/engine_scanbeam_services.h"
#include "clip/private/closed_clip_fast_path.h"
#include "clip/private/clip_execution_strategy.h"
#include "clip/private/clip_request_validation.h"
#include "clip/private/prepared_clip_runtime_data.h"

namespace clipper2next {

namespace internal {

auto configure_intersection_services(predicate_policy intersection_policy,
                                     engine_intersection_services& intersection_services) -> void;
auto build_request_clip_paths(engine_execution_context& context,
                              const execution_options& options,
                              paths64_result& result,
                              bool collect_closed_paths,
                              bool collect_open_paths) -> void;
auto build_request_clip_tree(engine_execution_context& context,
                             const execution_options& options,
                             clip_tree64_result& result) -> void;
[[nodiscard]] auto try_execute_nested_rectangle_union_tree(const clip_request64& request,
                                                           clip_tree64_result& result) -> bool;

}  // namespace internal

namespace {

struct reusable_engine_state_slot final {
    internal::clipper_base_state state{};
};

[[nodiscard]] auto reusable_engine_state_storage() -> reusable_engine_state_slot& {
    thread_local reusable_engine_state_slot slot;
    return slot;
}

[[nodiscard]] auto acquire_reusable_engine_state() -> internal::clipper_base_state& {
    auto& slot = reusable_engine_state_storage();
    internal::reset_engine_state_for_reuse(slot.state);
    return slot.state;
}

auto execute_clip_tree_into_impl(const clip_request64& request, clip_tree64_result& result)
    -> void {
    result.tree.clear();
    result.open.clear();

    auto& state = acquire_reusable_engine_state();
    const bool has_open_paths = !request.open_subjects.empty();
    bool succeeded = true;

    internal::add_paths_to_state(state, request.subjects, PathType::Subject, false);
    internal::add_paths_to_state(state, request.open_subjects, PathType::Subject, true);
    internal::add_paths_to_state(state, request.clips, PathType::Clip, false);

    internal::engine_execution_context context{state};
    internal::engine_scanbeam_processor processor{context};
    internal::engine_scanbeam_orchestration_options orchestration_options;
    orchestration_options.preserve_collinear = request.options.preserve_collinear;
    orchestration_options.has_open_paths = has_open_paths;
    internal::engine_intersection_services intersection_services;
    internal::configure_intersection_services(
        request.options.intersection_policy, intersection_services);
    internal::engine_scanbeam_services services{
        state, orchestration_options, succeeded, intersection_services};

    if (processor.execute(services, request.clip_type, request.fill_rule, true)) {
        internal::build_request_clip_tree(context, request.options, result);
    }
    internal::cleanup_engine_state(state);
}

}  // namespace

namespace internal {

auto release_clip_thread_state() noexcept -> void {
    auto& state = reusable_engine_state_storage().state;
    release_engine_state_storage(state);
}

namespace {

auto execute_clip_validated_with_schedule_mode(const clip_request64& request,
                                               scanbeam_schedule_mode schedule_mode)
    -> paths64_result {
    paths64_result result;
    auto& state = acquire_reusable_engine_state();
    const bool has_open_paths = !request.open_subjects.empty();
    bool succeeded = true;

    add_paths_to_state(state, request.subjects, PathType::Subject, false);
    add_paths_to_state(state, request.open_subjects, PathType::Subject, true);
    add_paths_to_state(state, request.clips, PathType::Clip, false);

    engine_execution_context context{state};
    engine_scanbeam_processor processor{context};
    engine_scanbeam_orchestration_options orchestration_options;
    orchestration_options.preserve_collinear = request.options.preserve_collinear;
    orchestration_options.has_open_paths = has_open_paths;
    orchestration_options.schedule_mode = schedule_mode;
    engine_intersection_services intersection_services;
    configure_intersection_services(
        request.options.intersection_policy, intersection_services);
    engine_scanbeam_services services{state, orchestration_options, succeeded, intersection_services};

    if (processor.execute(services, request.clip_type, request.fill_rule, false)) {
        const bool result_provably_empty =
            request.subjects.empty() && (request.clip_type == ClipType::Intersection ||
                                         request.clip_type == ClipType::Difference);
        const bool collect_closed_paths = !result_provably_empty;
        build_request_clip_paths(
            context, request.options, result, collect_closed_paths, has_open_paths);
    }
    cleanup_engine_state(state);
    return result;
}

}  // namespace

auto execute_clip_validated(const clip_request64& request) -> paths64_result {
    return execute_clip_validated_with_schedule_mode(request, scanbeam_schedule_mode::unit_runs);
}

auto execute_clip_validated_for_offset_cleanup(const clip_request64& request) -> paths64_result {
    return execute_clip_validated_with_schedule_mode(
        request, scanbeam_schedule_mode::monotone_runs);
}

auto execute_prepared_clip_runtime(const clip_request64& clip_request,
                                   const auto& runtime_data) -> paths64_result {
    if (runtime_data && runtime_data->cached_result.has_value()) {
        return *runtime_data->cached_result;
    }

    paths64_result result;
    auto& state = acquire_reusable_engine_state();
    bool has_open_paths = !clip_request.open_subjects.empty();
    bool succeeded = true;

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
    engine_scanbeam_processor processor{context};
    engine_scanbeam_orchestration_options orchestration_options;
    orchestration_options.preserve_collinear = clip_request.options.preserve_collinear;
    orchestration_options.has_open_paths = has_open_paths;
    engine_intersection_services intersection_services;
    configure_intersection_services(clip_request.options.intersection_policy,
                                    intersection_services);
    engine_scanbeam_services services{state, orchestration_options, succeeded, intersection_services};

    if (processor.execute(services, clip_request.clip_type, clip_request.fill_rule, false)) {
        const bool result_provably_empty = clip_request.subjects.empty() &&
                                           (clip_request.clip_type == ClipType::Intersection ||
                                            clip_request.clip_type == ClipType::Difference);
        const bool collect_closed_paths = !result_provably_empty;
        build_request_clip_paths(
            context, clip_request.options, result, collect_closed_paths, has_open_paths);
    }
    cleanup_engine_state(state);
    return result;
}

auto execute_clip_tree_validated(const clip_request64& request) -> clip_tree64_result {
    clip_tree64_result result;
    execute_clip_tree_into_impl(request, result);
    return result;
}

}  // namespace internal

auto clip(const clip_request64& request) -> paths64_result {
    return internal::execute_clip_with_fast_path(request);
}

auto clip(const prepared_clip_request64& request) -> paths64_result {
    const auto& clip_request = request.request();
    paths64_result result;
    if (internal::try_execute_closed_clip_fast_path(request, result)) { return result; }
    return internal::execute_prepared_clip_runtime(clip_request, request.runtime_data_);
}

auto clip_checked(const clip_request64& request) -> expected_paths64_result {
    if (!internal::clip_request_in_range(request)) {
        return make_clipper_error<paths64_result>(clipper_error_code::coordinate_range);
    }
    return internal::execute_clip_with_fast_path_validated(request);
}

auto clip_checked(const prepared_clip_request64& request) -> expected_paths64_result {
    if (!internal::clip_request_in_range(request.request()) ||
        !internal::clip_metadata_in_range(request.metadata())) {
        return make_clipper_error<paths64_result>(clipper_error_code::coordinate_range);
    }
    paths64_result result;
    if (internal::try_execute_closed_clip_fast_path(request, result)) { return result; }
    return internal::execute_prepared_clip_runtime(request.request(), request.runtime_data_);
}

auto clip_into(const clip_request64& request, paths64_result& result) -> void {
    internal::execute_clip_into_with_fast_path(request, result);
}

auto clip_into(const prepared_clip_request64& request, paths64_result& result) -> void {
    result = clip(request);
}

auto clip_tree(const clip_request64& request) -> clip_tree64_result {
    clip_tree64_result result;
    if (internal::try_execute_nested_rectangle_union_tree(request, result)) { return result; }
    execute_clip_tree_into_impl(request, result);
    return result;
}

auto clip_tree_checked(const clip_request64& request) -> expected_clip_tree64_result {
    clip_tree64_result result;
    if (!internal::clip_request_in_range(request)) {
        return make_clipper_error<clip_tree64_result>(clipper_error_code::coordinate_range);
    }
    if (internal::try_execute_nested_rectangle_union_tree(request, result)) { return result; }
    execute_clip_tree_into_impl(request, result);
    return result;
}

auto clip_tree_into(const clip_request64& request, clip_tree64_result& result) -> void {
    if (internal::try_execute_nested_rectangle_union_tree(request, result)) { return; }
    execute_clip_tree_into_impl(request, result);
}

}  // namespace clipper2next
