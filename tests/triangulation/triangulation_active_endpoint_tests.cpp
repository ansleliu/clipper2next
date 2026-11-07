#include <gtest/gtest.h>

#include "triangulation/private/triangulation_sweep_line.h"

#include <algorithm>
#include <array>

namespace next = clipper2next;
namespace in = next::internal;

namespace {
auto sector(in::triangulation_context& context, next::Point64 point, bool upper)
    -> std::size_t {
    auto* vertex = in::create_triangulation_vertex(context, point);
    auto* left = in::create_triangulation_vertex(context, {point.x - 1, point.y});
    auto* right = in::create_triangulation_vertex(context, {point.x + 1, point.y});
    auto* outgoing = in::create_triangulation_edge(context, vertex, upper ? right : left);
    auto* incoming = in::create_triangulation_edge(context, upper ? left : right, vertex);
    const auto id = context.boundary_events.size();
    outgoing->first_event = id;
    incoming->second_event = id;
    vertex->event_begin = id;
    vertex->event_end = id + 1;
    context.boundary_events.push_back({vertex, incoming, outgoing, in::boundary_event_kind::open, false});
    return id;
}

auto activate(in::triangulation_context& context, std::size_t id, bool both = false) -> void {
    const auto& event = context.boundary_events[id];
    in::add_sweep_active_edge(context, event.outgoing.get());
    if (both) { in::add_sweep_active_edge(context, event.incoming.get()); }
}

auto nearest(in::triangulation_context& context, std::size_t id)
    -> std::optional<in::triangulation_sweep_diagonal> {
    return in::nearest_sweep_diagonal(
        in::triangulation_sweep_front{context, context.boundary_events[id].vertex->pt, clipper2next::internal::prepare_triangulation_predicate_domain(context)}, id);
}
}  // namespace

TEST(Clipper2NextTriangulationActiveEndpointTests, EmptyFrontHasNoBridgeAndDoesNotPublish) {
    in::triangulation_context context;
    const auto minimum = sector(context, {0, 0}, true);
    EXPECT_FALSE(nearest(context, minimum));
    EXPECT_TRUE(context.triangles.empty());
    EXPECT_EQ(context.edges.size(), 2U);
}

TEST(Clipper2NextTriangulationActiveEndpointTests, EqualDistanceKeepsSweepPriorityAcrossInsertionOrder) {
    for (bool reverse : {false, true}) {
        in::triangulation_context context;
        const auto minimum = sector(context, {0, 0}, true);
        const auto left = sector(context, {-3, 10}, false);
        const auto right = sector(context, {3, 10}, false);
        activate(context, reverse ? left : right, true);
        activate(context, reverse ? right : left, true);
        const auto edges = context.edges.size();
        auto proof = nearest(context, minimum);
        ASSERT_TRUE(proof);
        EXPECT_EQ(proof->first()->pt, next::Point64(-3, 10));
        EXPECT_EQ(proof->second(), context.boundary_events[minimum].vertex.get());
        EXPECT_EQ(context.edges.size(), edges);
        EXPECT_TRUE(context.triangles.empty());
    }
}

TEST(Clipper2NextTriangulationActiveEndpointTests, InactiveClosestVertexCannotSetTheDistanceBound) {
    in::triangulation_context context;
    const auto minimum = sector(context, {0, 0}, true);
    (void)sector(context, {0, 2}, false);
    const auto farther = sector(context, {0, 10}, false);
    activate(context, farther);
    auto proof = nearest(context, minimum);
    ASSERT_TRUE(proof);
    EXPECT_EQ(proof->first()->pt, next::Point64(0, 10));
}

TEST(Clipper2NextTriangulationActiveEndpointTests, RejectedClosestBridgeCannotHideVisibleFartherBridge) {
    in::triangulation_context context;
    const auto minimum = sector(context, {0, 0}, true);
    const auto farther = sector(context, {8, 10}, false);
    const auto closest = sector(context, {0, 5}, false);
    activate(context, farther);
    activate(context, closest);
    auto* a = in::create_triangulation_vertex(context, {-1, 2});
    auto* b = in::create_triangulation_vertex(context, {1, 2});
    auto* blocker = in::create_triangulation_edge(context, a, b);
    in::add_sweep_active_edge(context, blocker);
    auto proof = nearest(context, minimum);
    ASSERT_TRUE(proof);
    EXPECT_EQ(proof->first()->pt, next::Point64(8, 10));
}

TEST(Clipper2NextTriangulationActiveEndpointTests, FullAdmittedExtentRetainsExactDistanceOrder) {
    constexpr auto m = next::MAX_COORD;
    in::triangulation_context context;
    const auto minimum = sector(context, {-m + 1, -m + 1}, true);
    const auto nearer = sector(context, {m - 2, m - 2}, false);
    const auto farther = sector(context, {m - 3, m - 1}, false);
    activate(context, nearer);
    activate(context, farther);
    auto proof = nearest(context, minimum);
    ASSERT_TRUE(proof);
    EXPECT_EQ(proof->first()->pt, next::Point64(m - 2, m - 2));
}

TEST(Clipper2NextTriangulationActiveEndpointTests, RejectedEqualDistanceAllowsNextSweepKey) {
    in::triangulation_context context;
    const auto minimum = sector(context, {0, 0}, true);
    const auto left = sector(context, {-3, 10}, false);
    const auto right = sector(context, {3, 10}, false);
    activate(context, left, true);
    activate(context, right, true);
    auto* a = in::create_triangulation_vertex(context, {-2, 4});
    auto* b = in::create_triangulation_vertex(context, {0, 4});
    in::add_sweep_active_edge(context, in::create_triangulation_edge(context, a, b));
    const auto edges = context.edges.size();
    auto* active = context.first_active.get();
    auto proof = nearest(context, minimum);
    ASSERT_TRUE(proof);
    EXPECT_EQ(proof->first()->pt, next::Point64(3, 10));
    EXPECT_EQ(context.edges.size(), edges);
    EXPECT_EQ(context.first_active.get(), active);
    EXPECT_TRUE(context.triangles.empty());
    EXPECT_FALSE(context.internal_error);
}

TEST(Clipper2NextTriangulationActiveEndpointTests, ExhaustedCandidatesDoNotPublishOrAlterFront) {
    in::triangulation_context context;
    const auto minimum = sector(context, {0, 0}, true);
    activate(context, sector(context, {-3, 10}, false), true);
    activate(context, sector(context, {3, 10}, false), true);
    auto* a = in::create_triangulation_vertex(context, {-10, 2});
    auto* b = in::create_triangulation_vertex(context, {10, 2});
    in::add_sweep_active_edge(context, in::create_triangulation_edge(context, a, b));
    const auto edges = context.edges.size();
    auto* active = context.first_active.get();
    EXPECT_FALSE(nearest(context, minimum));
    EXPECT_EQ(context.edges.size(), edges);
    EXPECT_EQ(context.first_active.get(), active);
    EXPECT_TRUE(context.triangles.empty());
    EXPECT_FALSE(context.internal_error);
}

TEST(Clipper2NextTriangulationActiveEndpointTests, InactiveWideGraphVertexPreservesExactNearestTie) {
    for(bool reverse:{false,true}) {
        in::triangulation_context context;
        const auto minimum=sector(context,{0,0},true);
        const auto left=sector(context,{-3,10},false);
        const auto right=sector(context,{3,10},false);
        (void)in::create_triangulation_vertex(context,{next::MAX_COORD,next::MIN_COORD});
        activate(context,reverse?left:right,true);
        activate(context,reverse?right:left,true);
        auto proof=nearest(context,minimum);
        ASSERT_TRUE(proof);EXPECT_EQ(proof->first()->pt,next::Point64(-3,10));
        EXPECT_TRUE(context.triangles.empty());
    }
}

TEST(Clipper2NextTriangulationActiveEndpointTests, WideDisplacementRetainsHighSquaredDistanceLimb) {
    in::triangulation_context context;
    const auto minimum=sector(context,{0,0},true);
    const auto nearer=sector(context,{0,10},false);
    const auto farther=sector(context,{int64_t{1}<<32,1},false);
    activate(context,nearer,true);activate(context,farther,true);
    auto proof=nearest(context,minimum);
    ASSERT_TRUE(proof);EXPECT_EQ(proof->first()->pt,next::Point64(0,10));
}
