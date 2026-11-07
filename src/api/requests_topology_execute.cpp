#include "api/private/borrowed_topology_pipeline.h"
#include "support/private/engine_resource_plan.h"

#include "clip/engine/private/engine_scanbeam_processor.h"
#include "clip/engine/private/engine_scanbeam_services.h"
#include "clip/private/borrowed_topology_access.h"
#include "clip/private/clip_request_validation.h"
#include "support/private/checked_size.h"

#include <type_traits>

namespace clipper2next::internal {
namespace {

[[nodiscard]] auto measure_input(const borrowed_clip_request64& request,
                                 borrowed_topology_workspace& workspace,
                                 topology_write_stats64& stats) -> clipper_error_code {
    if (const auto error = measure_paths(request.subjects,
                                         workspace.subjects,
                                         request.limits,
                                         stats.input_path_count,
                                         stats.input_point_count,
                                         stats.staging_reallocation_count);
        error != clipper_error_code::ok) {
        return error;
    }
    if (const auto error = measure_paths(request.clips,
                                         workspace.clips,
                                         request.limits,
                                         stats.input_path_count,
                                         stats.input_point_count,
                                         stats.staging_reallocation_count);
        error != clipper_error_code::ok) {
        return error;
    }

    return clipper_error_code::ok;
}

[[nodiscard]] auto load_input(const borrowed_clip_request64& request,
                              const borrowed_topology_workspace& workspace,
                              clipper_base_state& state,
                              topology_write_stats64& stats) -> clipper_error_code {
    if (const auto error =
            load_paths(state, request.subjects, workspace.subjects, PathType::Subject, stats);
        error != clipper_error_code::ok) {
        return error;
    }
    return load_paths(state, request.clips, workspace.clips, PathType::Clip, stats);
}

[[nodiscard]] auto run_engine(const borrowed_clip_request64& request,
                              clipper_base_state& state,
                              engine_execution_context& context) -> bool {
    engine_scanbeam_processor processor{context};
    engine_scanbeam_orchestration_options orchestration_options;
    orchestration_options.preserve_collinear = request.options.preserve_collinear;
    orchestration_options.has_open_paths = false;
    bool succeeded = true;
    engine_intersection_services intersection_services;
    intersection_services.intersection_policy = request.options.intersection_policy;
    engine_scanbeam_services services{
        state, orchestration_options, succeeded, intersection_services};
    return processor.execute(services, request.clip_type, request.fill_rule, true);
}

}  // namespace


namespace {

struct flat_clip_output final {
    path_set64& paths;
    scoped_workspace_charge& charge;
};

auto materialize_borrowed_output(
    topology_writer64& writer, engine_execution_context* context,
    const borrowed_clip_request64& request, borrowed_topology_workspace& workspace,
    execution_resource_ledger& resources, topology_write_stats64& stats) -> clipper_error_code {
    if (!context) {
        return write_topology(writer, request.options, {}, {}, 0U, resources.staging_high_water, stats);
    }
    std::size_t maximum_ring_point_count = 0U;
    if (const auto error = build_topology_descriptors(*context,
                                                      request.options,
                                                      request.limits,
                                                      workspace,
                                                      maximum_ring_point_count,
                                                      stats.staging_reallocation_count);
        error != clipper_error_code::ok) {
        return error;
    }

    stats.output_polygon_count = workspace.polygon_layouts.size();
    const auto& rings = workspace.closed_layout;
    stats.output_ring_count = rings.size();
    for (const auto& polygon : workspace.polygon_layouts) {
        stats.output_point_count = checked_size_add(stats.output_point_count, polygon.point_count);
    }
    if (const auto error = write_topology(writer,
                                          request.options,
                                          workspace.polygon_layouts,
                                          {rings.data(), rings.size()},
                                          maximum_ring_point_count,
                                          resources.staging_high_water,
                                          stats);
        error != clipper_error_code::ok) {
        return error;
    }

    return clipper_error_code::ok;
}

auto materialize_borrowed_output(
    flat_clip_output& output, engine_execution_context* context,
    const borrowed_clip_request64& request, borrowed_topology_workspace& workspace,
    execution_resource_ledger& resources, topology_write_stats64& stats) -> clipper_error_code {
    return write_borrowed_closed_paths(context, request.options, request.limits, workspace,
        output.paths, output.charge, resources, stats);
}

template <typename Output>
auto execute_borrowed_geometry(const borrowed_clip_request64& request,
                               Output& output,
                               execution_resource_ledger* resources)
    -> clipper_result<topology_write_stats64> {
    if (const auto error = validate_clip_parameters(
            request.clip_type, request.fill_rule, request.options.intersection_policy);
        error != clipper_error_code::ok) {
        return make_clipper_error<topology_write_stats64>(error);
    }
    if constexpr (std::is_same_v<Output, topology_writer64>) {
        if (!topology_writer64_access::is_bound(output)) {
            return make_clipper_error<topology_write_stats64>(clipper_error_code::sink_failure);
        }
    }

    auto local_resources = execution_resource_ledger{};
    if (!resources) {
        local_resources.maximum_work = request.limits.maximum_engine_work;
        local_resources.maximum_workspace_bytes = request.limits.maximum_engine_workspace_bytes;
        local_resources.maximum_staging_bytes = request.limits.maximum_staging_workspace_bytes;
        resources = &local_resources;
    }
    topology_write_stats64 stats;
    borrowed_topology_engine_state_lease lease{resources};
    auto& workspace = lease.workspace();
    if (const auto error = measure_input(request, workspace, stats);
        error != clipper_error_code::ok) {
        return make_clipper_error<topology_write_stats64>(error);
    }
    if (request.clip_type == ClipType::NoClip || stats.input_point_count == 0U) {
        if (const auto error = materialize_borrowed_output(
                output, nullptr, request, workspace, *resources, stats);
            error != clipper_error_code::ok) {
            return make_clipper_error<topology_write_stats64>(error);
        }
        return stats;
    }
    const auto engine_plan = plan_clip_engine_resources(stats.input_point_count);
    stats.planned_engine_work = engine_plan.work;
    stats.planned_engine_workspace_bytes = engine_plan.workspace_bytes;
    if (engine_plan.work == (std::numeric_limits<std::size_t>::max)() ||
        engine_plan.workspace_bytes == (std::numeric_limits<std::size_t>::max)() ||
        engine_plan.work > request.limits.maximum_engine_work ||
        engine_plan.workspace_bytes >
            request.limits.maximum_engine_workspace_bytes) {
        return make_clipper_error<topology_write_stats64>(
            clipper_error_code::resource_limit);
    }
    if (const auto error = resources->try_add_work(engine_plan.work); error != clipper_error_code::ok) {
        return make_clipper_error<topology_write_stats64>(error);
    }
    auto& state = lease.prepare_engine();
    if (const auto error = load_input(request, workspace, state, stats);
        error != clipper_error_code::ok) {
        return make_clipper_error<topology_write_stats64>(error);
    }

    engine_execution_context context{state};
    if (!run_engine(request, state, context)) {
        return make_clipper_error<topology_write_stats64>(clipper_error_code::internal_error);
    }

    if (const auto error = materialize_borrowed_output(
            output, &context, request, workspace, *resources, stats);
        error != clipper_error_code::ok) {
        return make_clipper_error<topology_write_stats64>(error);
    }
    stats.planned_engine_workspace_bytes = std::max(stats.planned_engine_workspace_bytes, resources->engine_high_water);
    return stats;
}

}  // namespace

auto execute_borrowed_topology(const borrowed_clip_request64& request,
                               topology_writer64& writer,
                               execution_resource_ledger* resources)
    -> clipper_result<topology_write_stats64> {
    return execute_borrowed_geometry(request, writer, resources);
}

auto execute_borrowed_closed_paths(const borrowed_clip_request64& request,
                                  path_set64& paths, scoped_workspace_charge& output_charge)
    -> clipper_error_code {
    auto output = flat_clip_output{paths, output_charge};
    const auto result = execute_borrowed_geometry(request, output, output_charge.context());
    return result ? clipper_error_code::ok : result.error();
}

}  // namespace clipper2next::internal
