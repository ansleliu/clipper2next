#include "clip/engine/private/engine_output_cleanup.h"

#include "clip/engine/private/engine_output.h"
#include "clipper2next/geometry.h"
#include "geometry/private/geometry_predicates.h"

#include <algorithm>

namespace clipper2next::internal {
namespace {

[[nodiscard]] auto segment_bounds_may_intersect(const Point64& first_start,
                                                 const Point64& first_end,
                                                 const Point64& second_start,
                                                 const Point64& second_end) noexcept -> bool {
    return (std::max)((std::min)(first_start.x, first_end.x),
                      (std::min)(second_start.x, second_end.x)) <=
               (std::min)((std::max)(first_start.x, first_end.x),
                          (std::max)(second_start.x, second_end.x)) &&
           (std::max)((std::min)(first_start.y, first_end.y),
                      (std::min)(second_start.y, second_end.y)) <=
               (std::min)((std::max)(first_start.y, first_end.y),
                          (std::max)(second_start.y, second_end.y));
}

}  // namespace

auto fix_self_intersections(OutRecList& output_records,
                            output_record_node* output_record,
                            const engine_output_cleanup_options& options) -> void {
    output_point_node* current = output_record->pts.get();
    if (current->prev == current->next->next) { return; }

    for (;;) {
        if (segments_properly_intersect_in_clipper_range(
                current->prev->pt, current->pt, current->next->pt, current->next->next->pt)) {
            if (segment_bounds_may_intersect(current->prev->pt,
                                             current->pt,
                                             current->next->next->pt,
                                             current->next->next->next->pt) &&
                segments_properly_intersect_in_clipper_range(
                    current->prev->pt,
                    current->pt,
                    current->next->next->pt,
                    current->next->next->next->pt)) {
                // Preserve legacy Clipper2's adjacent micro-self-intersection
                // topology: relocate a duplicate node before continuing the
                // scan instead of splitting two rings at a rounded near-touch.
                current = duplicate_out_point(current, false);
                current->pt = current->next->next->next->pt;
                current = current->next;
            } else {
                if (current == output_record->pts || current->next == output_record->pts) {
                    output_record->pts = output_record->pts->prev;
                }
                do_split_operation(output_records, output_record, current, options);
                if (!output_record->pts) { break; }
                current = output_record->pts;
                if (current->prev == current->next->next) { break; }
                continue;
            }
        }
        else {
            current = current->next;
        }
        if (current == output_record->pts) { break; }
    }
}

}  // namespace clipper2next::internal
