#include "clip/engine/private/engine_runtime_views.h"
#include "clip/engine/private/engine_output_owner.h"

#include <gtest/gtest.h>

#include <type_traits>
#include <vector>

namespace next = clipper2next;

TEST(Clipper2NextEngineRuntimeViewTests, ActiveEdgeViewExposesValueSemantics) {
    next::internal::active_edge_node edge{};
    edge.bottom = {0, 10};
    edge.top_point = {20, 30};
    edge.current_x = 12;
    edge.wind_dx = -1;

    const next::internal::active_edge_view view(edge);

    EXPECT_EQ(view.bottom(), next::Point64(0, 10));
    EXPECT_EQ(view.top(), next::Point64(20, 30));
    EXPECT_EQ(view.current_x(), 12);
    EXPECT_EQ(view.wind_delta(), -1);
    EXPECT_FALSE(view.has_output());
}

TEST(Clipper2NextEngineRuntimeViewTests, OutputRingViewMaterializesPointsWithoutPointerApi) {
    next::internal::engine_output_owner owner;
    auto* record = &owner.create_outrec();
    auto* first = &owner.create_outpt({0, 0}, *record);
    auto* second = &owner.create_outpt({10, 0}, *record);
    auto* third = &owner.create_outpt({10, 10}, *record);
    first->next = second;
    second->prev = first;
    second->next = third;
    third->prev = second;
    third->next = first;
    first->prev = third;

    const next::internal::output_ring_view view(*first);

    EXPECT_EQ(view.points(), (std::vector<next::Point64>{{0, 0}, {10, 0}, {10, 10}}));
    EXPECT_FALSE(view.is_isolated());
}

TEST(Clipper2NextEngineRuntimeViewTests, LocalMinimumViewExposesStableValues) {
    next::internal::Vertex vertex{};
    vertex.pt = {4, 8};
    next::internal::local_minimum_node minimum(vertex, next::PathType::Subject, true);

    const next::internal::local_minimum_view view(minimum);

    EXPECT_EQ(view.point(), next::Point64(4, 8));
    EXPECT_EQ(view.path_type(), next::PathType::Subject);
    EXPECT_TRUE(view.is_open_path());
}

TEST(Clipper2NextEngineRuntimeViewTests, RuntimeViewsDoNotReturnRawPointers) {
    using active_view = next::internal::active_edge_view;
    using output_view = next::internal::output_ring_view;
    using minimum_view = next::internal::local_minimum_view;

    static_assert(!std::is_pointer_v<decltype(std::declval<const active_view&>().bottom())>);
    static_assert(!std::is_pointer_v<decltype(std::declval<const active_view&>().top())>);
    static_assert(!std::is_pointer_v<decltype(std::declval<const output_view&>().points())>);
    static_assert(!std::is_pointer_v<decltype(std::declval<const minimum_view&>().point())>);
}
