#include "clip/engine/private/engine_intersection_processor.h"

#include <gtest/gtest.h>

namespace next = clipper2next;

TEST(Clipper2NextEngineIntersectionProcessorTests, IntersectionProcessorStartsClosedLocalMinForXor) {
    next::internal::clipper_base_state state;
    state.cliptype_ = next::ClipType::Xor;
    state.fillrule_ = next::FillRule::EvenOdd;
    next::internal::Vertex first_vertex{{0, 0}};
    next::internal::Vertex second_vertex{{10, 0}};
    next::internal::local_minimum_node first_minima{first_vertex, next::PathType::Subject, false};
    next::internal::local_minimum_node second_minima{second_vertex, next::PathType::Subject, false};
    next::internal::active_edge_node first;
    next::internal::active_edge_node second;
    first.local_min = &first_minima;
    second.local_min = &second_minima;
    first.winding_count = 1;
    second.winding_count = 1;
    first.wind_dx = 1;
    second.wind_dx = -1;
    bool succeeded = true;
    next::internal::intersect_edges(state, false, succeeded, first, second, {4, 5});

    EXPECT_TRUE(succeeded);
    ASSERT_EQ(state.output_owner_.records().size(), 1U);
    auto* output_record = state.output_owner_.records()[0].get();
    EXPECT_EQ(first.outrec.get(), output_record);
    EXPECT_EQ(second.outrec.get(), output_record);
    EXPECT_EQ(output_record->pts->pt, next::Point64(4, 5));
}
