#include "clipper2next/clip.h"

#include "clip/engine/private/engine_execution_context.h"
#include "clip/engine/private/engine_output_cleanup.h"
#include "clip/engine/private/engine_path_builder.h"
#include "clip/engine/private/engine_scanbeam_services.h"
#include "clip/engine/private/engine_winding.h"
#include "support/private/checked_size.h"

#include <memory>
#include <vector>

namespace clipper2next::internal {
auto configure_intersection_services(predicate_policy intersection_policy,
                                     engine_intersection_services& intersection_services) -> void {
    intersection_services.intersection_policy = intersection_policy;
}

auto build_request_clip_paths(engine_execution_context& context,
                              const execution_options& options,
                              paths64_result& result,
                              bool collect_closed_paths,
                              bool collect_open_paths) -> void {
    auto& output_records = context.output_owner().records();
    engine_output_cleanup_options cleanup_options;
    cleanup_options.preserve_collinear = options.preserve_collinear;
    cleanup_options.reverse_solution = options.reverse_solution;
    cleanup_options.using_polytree = false;

    result.closed.clear();
    result.open.clear();
    if (collect_closed_paths) { result.closed.reserve(output_records.size()); }
    if (collect_open_paths) { result.open.reserve(output_records.size()); }
    for (std::size_t index = 0; index < output_records.size(); ++index) {
        auto* output_record = output_records[index].get();
        if (!output_record || !output_record->pts) { continue; }

        Path64 path;
        if (output_record->is_open) {
            if (!collect_open_paths) { continue; }
            if (build_path64(output_record->pts, options.reverse_solution, true, path)) {
                result.open.emplace_back(std::move(path));
            }
            continue;
        }

        if (!collect_closed_paths) { continue; }

        clean_collinear(output_records, output_record, cleanup_options);
        if (build_path64(output_record->pts, options.reverse_solution, false, path)) {
            result.closed.emplace_back(std::move(path));
        }
    }
}

auto build_request_clip_tree(engine_execution_context& context,
                             const execution_options& options,
                             clip_tree64_result& result) -> void {
    auto& output_records = context.output_owner().records();
    engine_output_cleanup_options cleanup_options;
    cleanup_options.preserve_collinear = options.preserve_collinear;
    cleanup_options.reverse_solution = options.reverse_solution;
    cleanup_options.using_polytree = true;

    result.tree.clear();
    result.tree.reserve(checked_size_add(output_records.size(), 1U));
    result.tree.reserve_children(result.tree.root(), output_records.size());
    result.open.clear();
    result.open.reserve(output_records.size());
    for (const auto& record : output_records) {
        if (!record) { continue; }
        record->path.clear();
    }

    for (std::size_t index = 0; index < output_records.size(); ++index) {
        auto* output_record = output_records[index].get();
        if (!output_record || !output_record->pts) { continue; }

        if (output_record->is_open) {
            Path64 path;
            if (build_path64(output_record->pts, options.reverse_solution, true, path)) {
                result.open.emplace_back(std::move(path));
            }
            continue;
        }

        if (check_bounds(output_records, output_record, cleanup_options)) {
            recursive_check_owners(output_records,
                                   output_record,
                                   result.tree,
                                   result.tree.root(),
                                   cleanup_options);
        }
    }
}

}  // namespace clipper2next::internal
