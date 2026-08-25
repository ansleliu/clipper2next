#include "clip/engine/private/engine_output_cleanup.h"
#include "clip/engine/private/engine_output.h"
#include "clip/engine/private/engine_state.h"

#include <gtest/gtest.h>

#include <initializer_list>

namespace next = clipper2next;

namespace {

auto make_output_ring(next::internal::output_record_node& output_record,
                      std::initializer_list<next::Point64> points)
    -> next::internal::output_point_node* {
    auto it = points.begin();
    auto* first = &output_record.output_owner->create_outpt(*it, output_record);
    output_record.pts = first;
    auto* previous = first;
    ++it;
    for (; it != points.end(); ++it) {
        auto* current = &output_record.output_owner->create_outpt(*it, output_record);
        current->prev = previous;
        current->next = first;
        previous->next = current;
        first->prev = current;
        previous = current;
    }
    return first;
}

auto ring_to_path(next::internal::output_point_node* output_point) -> next::Path64 {
    next::Path64 result;
    auto* current = output_point;
    do {
        result.push_back(current->pt);
        current = current->next.get();
    } while (current != output_point);
    return result;
}

}  // namespace

TEST(Clipper2NextEngineOutputCleanupTests, OutputCleanupRemovesDuplicateClosedVertices) {
    next::internal::clipper_base_state state;
    auto* output_record = &state.output_owner_.create_outrec();
    make_output_ring(*output_record, {{0, 0}, {10, 0}, {10, 0}, {10, 10}, {0, 10}});
    next::internal::engine_output_cleanup_options options;
    options.preserve_collinear = true;
    next::internal::clean_collinear(
        state.output_owner_.records(), output_record, options);

    ASSERT_NE(output_record->pts, nullptr);
    EXPECT_EQ(next::internal::point_count(output_record->pts), 4);
}

TEST(Clipper2NextEngineOutputCleanupTests, BoundsScanDoesNotMaterializeAnOwningPath) {
    next::internal::clipper_base_state state;
    auto* output_record = &state.output_owner_.create_outrec();
    make_output_ring(*output_record, {{0, 0}, {10, 0}, {10, 10}, {0, 10}});
    next::internal::engine_output_cleanup_options options;
    options.preserve_collinear = true;
    options.using_polytree = true;
    options.path_storage = next::internal::output_path_storage::linked_points;
    const auto valid = next::internal::check_bounds(
        state.output_owner_.records(), output_record, options);

    EXPECT_TRUE(valid);
    EXPECT_EQ(output_record->bounds, (next::Rect64{0, 0, 10, 10}));
    EXPECT_TRUE(output_record->path.empty());
}

TEST(Clipper2NextEngineOutputCleanupTests, BoundsScanMaterializesPathForOwningTreeOutput) {
    next::internal::clipper_base_state state;
    auto* output_record = &state.output_owner_.create_outrec();
    make_output_ring(*output_record, {{0, 0}, {10, 0}, {10, 10}, {0, 10}});
    next::internal::engine_output_cleanup_options options;
    options.preserve_collinear = true;
    options.using_polytree = true;
    options.path_storage = next::internal::output_path_storage::owning_path;
    const auto valid = next::internal::check_bounds(
        state.output_owner_.records(), output_record, options);

    EXPECT_TRUE(valid);
    EXPECT_EQ(output_record->bounds, (next::Rect64{0, 0, 10, 10}));
    EXPECT_EQ(output_record->path.size(), 4U);
}

TEST(Clipper2NextEngineOutputCleanupTests, CleanupLocalCompactionRemovesInteriorDuplicate) {
    next::internal::clipper_base_state state;
    auto* output_record = &state.output_owner_.create_outrec();
    make_output_ring(*output_record, {{0, 0}, {10, 0}, {10, 0}, {10, 10}, {0, 10}});
    next::internal::engine_output_cleanup_options options;
    options.preserve_collinear = true;

    const auto result = next::internal::compact_local_collinear_nodes(*output_record, options);

    EXPECT_TRUE(result.removed_any);
    EXPECT_FALSE(result.invalidated_path);
    ASSERT_NE(result.stable_start, nullptr);
    ASSERT_NE(output_record->pts, nullptr);
    EXPECT_EQ(ring_to_path(output_record->pts), (next::Path64{{0, 0}, {10, 0}, {10, 10}, {0, 10}}));
}

TEST(Clipper2NextEngineOutputCleanupTests, CleanupLocalCompactionPreservesForwardCollinearPoint) {
    next::internal::clipper_base_state state;
    auto* output_record = &state.output_owner_.create_outrec();
    make_output_ring(*output_record, {{0, 0}, {5, 0}, {10, 0}, {10, 10}, {0, 10}});
    next::internal::engine_output_cleanup_options options;
    options.preserve_collinear = true;

    const auto result = next::internal::compact_local_collinear_nodes(*output_record, options);

    EXPECT_FALSE(result.removed_any);
    EXPECT_FALSE(result.invalidated_path);
    ASSERT_NE(output_record->pts, nullptr);
    EXPECT_EQ(next::internal::point_count(output_record->pts), 5);
}

TEST(Clipper2NextEngineOutputCleanupTests,
     CleanupLocalCompactionRemovesForwardCollinearWhenNotPreserved) {
    next::internal::clipper_base_state state;
    auto* output_record = &state.output_owner_.create_outrec();
    make_output_ring(*output_record, {{0, 0}, {5, 0}, {10, 0}, {10, 10}, {0, 10}});
    next::internal::engine_output_cleanup_options options;
    options.preserve_collinear = false;

    const auto result = next::internal::compact_local_collinear_nodes(*output_record, options);

    EXPECT_TRUE(result.removed_any);
    EXPECT_FALSE(result.invalidated_path);
    ASSERT_NE(output_record->pts, nullptr);
    EXPECT_EQ(ring_to_path(output_record->pts), (next::Path64{{0, 0}, {10, 0}, {10, 10}, {0, 10}}));
}

TEST(Clipper2NextEngineOutputCleanupTests,
     CleanupLocalCompactionRemovesBacktrackingCollinearWhenPreserved) {
    next::internal::clipper_base_state state;
    auto* output_record = &state.output_owner_.create_outrec();
    make_output_ring(*output_record, {{0, 0}, {10, 0}, {5, 0}, {10, 10}, {0, 10}});
    next::internal::engine_output_cleanup_options options;
    options.preserve_collinear = true;

    const auto result = next::internal::compact_local_collinear_nodes(*output_record, options);

    EXPECT_TRUE(result.removed_any);
    EXPECT_FALSE(result.invalidated_path);
    ASSERT_NE(output_record->pts, nullptr);
    EXPECT_EQ(next::internal::point_count(output_record->pts), 4);
}

TEST(Clipper2NextEngineOutputCleanupTests, CleanupLocalCompactionInvalidatesTooSmallPath) {
    next::internal::clipper_base_state state;
    auto* output_record = &state.output_owner_.create_outrec();
    make_output_ring(*output_record, {{0, 0}, {0, 0}, {1, 0}, {0, 1}});
    next::internal::engine_output_cleanup_options options;
    options.preserve_collinear = true;

    const auto result = next::internal::compact_local_collinear_nodes(*output_record, options);

    EXPECT_TRUE(result.removed_any);
    EXPECT_TRUE(result.invalidated_path);
    EXPECT_EQ(result.stable_start, nullptr);
    EXPECT_EQ(output_record->pts, nullptr);
}
