#include "clip/engine/private/engine_output_cleanup.h"

#include "clipper2next/geometry.h"
#include "clip/engine/private/engine_output.h"
#include "clip/engine/private/engine_path_builder.h"
#include "geometry/private/geometry_predicates.h"

namespace clipper2next::internal {
namespace {

[[nodiscard]] auto is_valid_closed_path(const output_point_node* output_point) -> bool {
    return output_point && (output_point->next != output_point) &&
           (output_point->next != output_point->prev) && !is_very_small_triangle(*output_point);
}

}  // namespace

auto compact_local_collinear_nodes(output_record_node& output_record,
                                   const engine_output_cleanup_options& options)
    -> cleanup_local_scan_result {
    cleanup_local_scan_result result;
    if (output_record.is_open) {
        result.stable_start = output_record.pts.get();
        return result;
    }

    if (!is_valid_closed_path(output_record.pts)) {
        dispose_out_points(&output_record);
        result.invalidated_path = true;
        return result;
    }

    output_point_node* start_point = output_record.pts;
    output_point_node* current = start_point;
    for (;;) {
        if (cross_product_sign_in_clipper_range(
                current->prev->pt, current->pt, current->next->pt) == 0 &&
            (current->pt == current->prev->pt || current->pt == current->next->pt ||
             !options.preserve_collinear ||
             dot_product(current->prev->pt, current->pt, current->next->pt) < 0)) {
            if (current == output_record.pts) { output_record.pts = current->prev; }

            current = dispose_out_point(current);
            result.removed_any = true;
            if (!is_valid_closed_path(current)) {
                dispose_out_points(&output_record);
                result.invalidated_path = true;
                result.stable_start = nullptr;
                return result;
            }
            start_point = current;
            continue;
        }

        current = current->next;
        if (current == start_point) { break; }
    }
    result.stable_start = output_record.pts.get();
    return result;
}

}  // namespace clipper2next::internal
