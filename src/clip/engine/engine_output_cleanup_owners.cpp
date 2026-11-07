#include "clip/engine/private/engine_output_cleanup.h"

#include "clip/engine/private/engine_topology.h"

namespace clipper2next::internal {
auto resolve_output_record_owner(OutRecList& output_records,
                                 output_record_node* output_record,
                                 const engine_output_cleanup_options& options) -> void {
    if (!output_record || output_record->bounds.is_empty()) { return; }
    while (output_record->owner) {
        if (!output_record->owner->splits.empty() &&
            check_split_owner(
                output_records, output_record, &output_record->owner->splits, options)) {
            break;
        }
        if (output_record->owner->pts &&
            check_bounds(output_records, output_record->owner, options) &&
            output_record->owner->bounds.contains(output_record->bounds) &&
            path2_contains_path1(output_record->pts, output_record->owner->pts)) {
            break;
        }
        output_record->owner = output_record->owner->owner;
    }
}

}  // namespace clipper2next::internal
