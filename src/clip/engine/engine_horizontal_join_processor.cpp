#include "clip/engine/private/engine_horizontal.h"

#include "clip/engine/private/engine_output_owner.h"

namespace clipper2next::internal {
namespace {

auto fix_output_record_points(output_record_node* output_record) -> void {
    auto* output_point = output_record->pts.get();
    do {
        output_point->outrec = output_record;
        output_point = output_point->next.get();
    } while (output_point != output_record->pts.get());
}

}  // namespace

auto process_horizontal_joins(std::vector<horizontal_join_node>& horizontal_joins,
                              engine_output_owner& output_owner,
                              bool using_polytree) -> void {
    for (auto& join : horizontal_joins) {
        auto& first_point = join.first_point();
        auto& second_point = join.second_point();
        auto* first_record = get_real_outrec(first_point.outrec);
        auto* second_record = get_real_outrec(second_point.outrec);

        auto* first_back = first_point.next.get();
        auto* second_back = second_point.prev.get();
        first_point.next = &second_point;
        second_point.prev = &first_point;
        first_back->prev = second_back;
        second_back->next = first_back;

        if (first_record == second_record) {
            second_record = &output_owner.create_outrec();
            second_record->pts = first_back;
            fix_output_record_points(second_record);

            if (first_record->pts->outrec == second_record) {
                first_record->pts = &first_point;
                first_record->pts->outrec = first_record;
            }

            if (using_polytree) {
                if (path2_contains_path1(first_record->pts, second_record->pts)) {
                    auto* points = first_record->pts.get();
                    first_record->pts = second_record->pts;
                    second_record->pts = points;
                    fix_output_record_points(first_record);
                    fix_output_record_points(second_record);
                    second_record->owner = first_record;
                } else if (path2_contains_path1(second_record->pts, first_record->pts)) {
                    second_record->owner = first_record;
                } else {
                    second_record->owner = first_record->owner;
                }

                first_record->splits.emplace_back(second_record);
            } else {
                second_record->owner = first_record;
            }
        } else {
            second_record->pts = nullptr;
            if (using_polytree) {
                set_owner(second_record, first_record);
                if (!second_record->splits.empty()) { move_splits(second_record, first_record); }
            } else {
                second_record->owner = first_record;
            }
        }
    }
}

}  // namespace clipper2next::internal
