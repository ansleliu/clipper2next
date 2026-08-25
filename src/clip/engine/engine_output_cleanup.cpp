#include "clip/engine/private/engine_output_cleanup.h"

#include "clipper2next/geometry.h"
#include "clip/engine/private/engine_output.h"
#include "clip/engine/private/engine_path_builder.h"
#include "clip/engine/private/engine_topology.h"
#include "geometry/private/geometry_predicates.h"

namespace clipper2next::internal {
namespace {

[[nodiscard]] auto is_valid_closed_path(const output_point_node* output_point) -> bool {
    return output_point && (output_point->next != output_point) &&
           (output_point->next != output_point->prev) && !is_very_small_triangle(*output_point);
}

}  // namespace

auto clean_collinear(OutRecList& output_records,
                     output_record_node* output_record,
                     const engine_output_cleanup_options& options) -> void {
    output_record = get_real_outrec(output_record);
    if (!output_record || output_record->is_open) { return; }
    if (!is_valid_closed_path(output_record->pts)) {
        dispose_out_points(output_record);
        return;
    }

#if defined(CLIPPER2NEXT_USE_CLEANUP_LOCAL_COMPACT)
    const cleanup_local_scan_result local_scan =
        compact_local_collinear_nodes(*output_record, options);
    if (local_scan.invalidated_path) { return; }
#else
    output_point_node* start_point = output_record->pts;
    output_point_node* current = start_point;
    for (;;) {
        if (cross_product_sign_in_clipper_range(
                current->prev->pt, current->pt, current->next->pt) == 0 &&
            (current->pt == current->prev->pt || current->pt == current->next->pt ||
             !options.preserve_collinear ||
             dot_product(current->prev->pt, current->pt, current->next->pt) < 0)) {
            if (current == output_record->pts) { output_record->pts = current->prev; }

            current = dispose_out_point(current);
            if (!is_valid_closed_path(current)) {
                dispose_out_points(output_record);
                return;
            }
            start_point = current;
            continue;
        }

        current = current->next;
        if (current == start_point) { break; }
    }
#endif
    fix_self_intersections(output_records, output_record, options);
}

}  // namespace clipper2next::internal
