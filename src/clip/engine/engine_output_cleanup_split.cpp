#include "clip/engine/private/engine_output_cleanup.h"

#include "clipper2next/geometry.h"
#include "clip/engine/private/engine_output.h"
#include "clip/engine/private/engine_output_owner.h"
#include "clip/engine/private/engine_topology.h"

#include <cmath>

namespace clipper2next::internal {
namespace {

[[nodiscard]] auto create_output_record(engine_output_owner& output_owner) -> output_record_node* {
    return &output_owner.create_outrec();
}

[[nodiscard]] auto area(output_point_node* output_point) -> double {
    auto result = 0.0;
    auto* current = output_point;
    do {
        result += static_cast<double>(current->prev->pt.y + current->pt.y) *
                  static_cast<double>(current->prev->pt.x - current->pt.x);
        current = current->next;
    } while (current != output_point);
    return result * 0.5;
}

[[nodiscard]] auto triangle_area(const Point64& first, const Point64& second, const Point64& third)
    -> double {
    return static_cast<double>(third.y + first.y) * static_cast<double>(third.x - first.x) +
           static_cast<double>(first.y + second.y) * static_cast<double>(first.x - second.x) +
           static_cast<double>(second.y + third.y) * static_cast<double>(second.x - third.x);
}

}  // namespace

auto do_split_operation(OutRecList& output_records,
                        output_record_node* output_record,
                        output_point_node* split_point,
                        const engine_output_cleanup_options& options) -> void {
    static_cast<void>(output_records);
    auto& split = *split_point;
    auto& previous = *split.prev;
    auto& split_next = *split.next;
    auto& next_next = *split_next.next;
    output_record->pts = &previous;

    Point64 intersection;
    static_cast<void>(
        line_intersection_point(previous.pt, split.pt, split_next.pt, next_next.pt, intersection));

    const double first_area = area(output_record->pts);
    const double first_abs_area = std::fabs(first_area);
    if (first_abs_area < 2) {
        dispose_out_points(output_record);
        return;
    }

    const double second_area = triangle_area(intersection, split.pt, split_next.pt);
    const double second_abs_area = std::fabs(second_area);

    if (intersection == previous.pt || intersection == next_next.pt) {
        next_next.prev = &previous;
        previous.next = &next_next;
    } else {
        auto& new_point = output_record->output_owner->create_outpt(intersection, *previous.outrec);
        new_point.prev = &previous;
        new_point.next = &next_next;
        next_next.prev = &new_point;
        previous.next = &new_point;
    }

    if (second_abs_area >= 1 &&
        (second_abs_area > first_abs_area || ((second_area > 0) == (first_area > 0)))) {
        output_record_node* new_output_record = create_output_record(*output_record->output_owner);
        new_output_record->owner = output_record->owner;

        split.outrec = new_output_record;
        split_next.outrec = new_output_record;
        auto& new_point =
            output_record->output_owner->create_outpt(intersection, *new_output_record);
        new_point.prev = &split_next;
        new_point.next = &split;
        new_output_record->pts = &new_point;
        split.prev = &new_point;
        split_next.next = &new_point;

        if (options.using_polytree) {
            if (path2_contains_path1(&previous, &new_point)) {
                new_output_record->splits.emplace_back(output_record);
            } else {
                output_record->splits.emplace_back(new_output_record);
            }
        }
    } else {
        split_next.next = &split_next;
        split_next.prev = &split_next;
        split_next.outrec = nullptr;
        split.next = &split;
        split.prev = &split;
        split.outrec = nullptr;
    }
}

}  // namespace clipper2next::internal
