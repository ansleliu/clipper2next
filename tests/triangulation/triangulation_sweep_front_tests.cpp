#include "triangulation/private/triangulation_sweep_front.h"
#include "triangulation/private/triangulation_sweep_line.h"
#include "triangulation/private/triangulation_topology.h"
#include <gtest/gtest.h>

namespace next=clipper2next;
namespace in=next::internal;

TEST(Clipper2NextTriangulationSweepFrontTests, PhaseKeepsExactHorizontalSweepOrder) {
    in::triangulation_context context;
    const in::triangulation_sweep_front front{context,{4,10}, clipper2next::internal::prepare_triangulation_predicate_domain(context)};
    EXPECT_TRUE(front.includes({3,10}));
    EXPECT_TRUE(front.includes({4,10}));
    EXPECT_TRUE(front.includes({100,11}));
    EXPECT_FALSE(front.includes({5,10}));
    EXPECT_FALSE(front.includes({-100,9}));
    EXPECT_EQ(&front.context(),&context);
    EXPECT_EQ(front.minimum(),next::Point64(4,10));
}

TEST(Clipper2NextTriangulationSweepFrontTests, ActiveConstraintCrossingBlocksDiagonal) {
    in::triangulation_context context;
    auto* a=in::create_triangulation_vertex(context,{0,0});
    auto* b=in::create_triangulation_vertex(context,{10,10});
    auto* c=in::create_triangulation_vertex(context,{0,10});
    auto* d=in::create_triangulation_vertex(context,{10,0});
    auto* constraint=in::create_triangulation_edge(context,c,d,in::triangulation_edge_kind::ascend);
    in::add_sweep_active_edge(context,constraint);
    const in::triangulation_sweep_front front{context,{20,-1}, clipper2next::internal::prepare_triangulation_predicate_domain(context)};
    EXPECT_FALSE(in::triangulation_sweep_diagonal::probe(front,a,b));
}

TEST(Clipper2NextTriangulationSweepFrontTests, InteriorEndpointTouchBlocksWithoutProperCrossing) {
    in::triangulation_context context;
    auto* a=in::create_triangulation_vertex(context,{0,0});
    auto* b=in::create_triangulation_vertex(context,{10,10});
    auto* c=in::create_triangulation_vertex(context,{5,5});
    auto* d=in::create_triangulation_vertex(context,{0,10});
    auto* constraint=in::create_triangulation_edge(context,c,d,in::triangulation_edge_kind::ascend);
    in::add_sweep_active_edge(context,constraint);
    const in::triangulation_sweep_front front{context,{20,-1}, clipper2next::internal::prepare_triangulation_predicate_domain(context)};
    EXPECT_FALSE(in::triangulation_sweep_diagonal::probe(front,a,b));
}

TEST(Clipper2NextTriangulationSweepFrontTests, SharedEndpointDoesNotBlockAnotherDirection) {
    in::triangulation_context context;
    auto* a=in::create_triangulation_vertex(context,{0,0});
    auto* b=in::create_triangulation_vertex(context,{10,10});
    auto* c=in::create_triangulation_vertex(context,{-10,10});
    auto* constraint=in::create_triangulation_edge(context,a,c,in::triangulation_edge_kind::ascend);
    in::add_sweep_active_edge(context,constraint);
    const in::triangulation_sweep_front front{context,{20,-1}, clipper2next::internal::prepare_triangulation_predicate_domain(context)};
    EXPECT_TRUE(in::triangulation_sweep_diagonal::probe(front,a,b));
    EXPECT_FALSE(in::triangulation_sweep_diagonal::probe(front,a,c));
    EXPECT_FALSE(in::triangulation_sweep_diagonal::probe(front,a,a));
}

TEST(Clipper2NextTriangulationSweepFrontTests, LooseEdgesFollowTheirActualActivity) {
    in::triangulation_context context;
    auto* a=in::create_triangulation_vertex(context,{0,0});
    auto* b=in::create_triangulation_vertex(context,{10,10});
    auto* c=in::create_triangulation_vertex(context,{0,10});
    auto* d=in::create_triangulation_vertex(context,{10,0});
    auto* loose=in::create_triangulation_edge(context,c,d);
    const in::triangulation_sweep_front front{context,{20,-1}, clipper2next::internal::prepare_triangulation_predicate_domain(context)};
    EXPECT_TRUE(in::triangulation_sweep_diagonal::probe(front,a,b));
    in::add_sweep_active_edge(context,loose);
    EXPECT_FALSE(in::triangulation_sweep_diagonal::probe(front,a,b));
    in::remove_sweep_active_edge(context,loose);
    ASSERT_FALSE(context.internal_error);
    EXPECT_TRUE(in::triangulation_sweep_diagonal::probe(front,a,b));
}

TEST(Clipper2NextTriangulationSweepFrontTests, FutureConstraintIsOutsideAdmittedPhaseSegments) {
    in::triangulation_context context;
    auto* a=in::create_triangulation_vertex(context,{0,0});
    auto* b=in::create_triangulation_vertex(context,{10,10});
    auto* c=in::create_triangulation_vertex(context,{0,-10});
    auto* d=in::create_triangulation_vertex(context,{10,-20});
    (void)in::create_triangulation_edge(context,c,d,in::triangulation_edge_kind::ascend);
    const in::triangulation_sweep_front front{context,{0,0}, clipper2next::internal::prepare_triangulation_predicate_domain(context)};
    EXPECT_FALSE(front.includes(c->pt));
    EXPECT_FALSE(front.includes(d->pt));
    EXPECT_TRUE(in::triangulation_sweep_diagonal::probe(front,a,b));
}

TEST(Clipper2NextTriangulationSweepFrontTests, FullAdmittedRangeContactHasNoRoundedSideTest) {
    constexpr auto m=next::MAX_COORD;
    in::triangulation_context context;
    auto* a=in::create_triangulation_vertex(context,{-m,-m});
    auto* b=in::create_triangulation_vertex(context,{m,m});
    auto* c=in::create_triangulation_vertex(context,{0,0});
    auto* d=in::create_triangulation_vertex(context,{-m,m});
    auto* constraint=in::create_triangulation_edge(context,c,d,in::triangulation_edge_kind::ascend);
    in::add_sweep_active_edge(context,constraint);
    const in::triangulation_sweep_front front{context,{m,-m}, clipper2next::internal::prepare_triangulation_predicate_domain(context)};
    EXPECT_FALSE(in::triangulation_sweep_diagonal::probe(front,a,b));
}

TEST(Clipper2NextTriangulationSweepFrontTests, ResetRemovesOldFrontierBeforeRebuilding) {
    in::triangulation_context context;
    auto* a=in::create_triangulation_vertex(context,{0,0});
    auto* b=in::create_triangulation_vertex(context,{10,10});
    auto* c=in::create_triangulation_vertex(context,{5,5});
    auto* edge=in::create_triangulation_edge(context,c,b);
    in::add_sweep_active_edge(context,edge);
    EXPECT_FALSE(in::triangulation_sweep_diagonal::probe(in::triangulation_sweep_front{context,{20,-1}, clipper2next::internal::prepare_triangulation_predicate_domain(context)},a,b));
    context.clear();
    a=in::create_triangulation_vertex(context,{0,0});
    b=in::create_triangulation_vertex(context,{10,10});
    EXPECT_TRUE(in::triangulation_sweep_diagonal::probe(in::triangulation_sweep_front{context,{20,-1}, clipper2next::internal::prepare_triangulation_predicate_domain(context)},a,b));
    context.release();
}
