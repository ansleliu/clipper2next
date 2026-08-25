#include "clip/engine/private/engine_output_cleanup.h"

#include "clipper2next/geometry.h"
#include "clip/engine/private/engine_path_builder.h"
#include "clip/engine/private/engine_topology.h"

#include <algorithm>

namespace clipper2next::internal {

auto check_bounds(OutRecList& output_records,
                  output_record_node* output_record,
                  const engine_output_cleanup_options& options) -> bool {
    if (!output_record->pts) { return false; }
    if (!output_record->bounds.is_empty()) { return true; }
    clean_collinear(output_records, output_record, options);
    if (!output_record->pts) { return false; }

    if (options.path_storage == output_path_storage::owning_path) {
        if (!build_path64(
                output_record->pts, options.reverse_solution, false, output_record->path)) {
            return false;
        }
        output_record->bounds = bounds(output_record->path);
        return true;
    }

    auto bounds = Rect64::invalid_rect();
    auto* current = output_record->pts.get();
    do {
        bounds.left = (std::min)(bounds.left, current->pt.x);
        bounds.top = (std::min)(bounds.top, current->pt.y);
        bounds.right = (std::max)(bounds.right, current->pt.x);
        bounds.bottom = (std::max)(bounds.bottom, current->pt.y);
        current = current->next.get();
    } while (current != output_record->pts);
    output_record->bounds = bounds;
    return true;
}

auto check_split_owner(OutRecList& output_records,
                       output_record_node* output_record,
                       OutRecList* splits,
                       const engine_output_cleanup_options& options) -> bool {
    for (size_t index = 0; index < splits->size(); ++index) {
        output_record_node* split = (*splits)[index];
        if (!split->pts && !split->splits.empty() &&
            check_split_owner(output_records, output_record, &split->splits, options)) {
            return true;
        }

        split = get_real_outrec(split);
        if (!split || split == output_record || split->recursive_split == output_record) {
            continue;
        }
        split->recursive_split = output_record;

        if (!split->splits.empty() &&
            check_split_owner(output_records, output_record, &split->splits, options)) {
            return true;
        }

        if (!check_bounds(output_records, split, options) ||
            !split->bounds.contains(output_record->bounds) ||
            !path2_contains_path1(output_record->pts, split->pts)) {
            continue;
        }

        if (!is_valid_owner(output_record, split)) { split->owner = output_record->owner; }

        output_record->owner = split;
        return true;
    }
    return false;
}

}  // namespace clipper2next::internal
