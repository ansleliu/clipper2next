#include "triangulation/private/triangulation_sweep_diagonal.h"
#include "triangulation/private/triangulation_boundary.h"
#include "triangulation/private/triangulation_sweep_line.h"
#include <gtest/gtest.h>
#include <algorithm>
#include <type_traits>
#include <utility>

namespace next=clipper2next;
namespace in=next::internal;

static_assert(!std::is_default_constructible_v<in::triangulation_sweep_diagonal>);
static_assert(!std::is_copy_constructible_v<in::triangulation_sweep_diagonal>);

namespace {
auto vertex(in::triangulation_context& context,next::Point64 point)->in::triangulation_vertex* {
    const auto found=std::find_if(context.vertices.begin(),context.vertices.end(),[&](auto value){return value->pt==point;});
    return found==context.vertices.end()?nullptr:found->get();
}
auto prepare_square(in::triangulation_context& context)->void {
    ASSERT_EQ(in::build_triangulation_boundary(context,{{{0,0},{10,0},{10,10},{0,10}}}),next::TriangulateResult::success);
    for(auto edge:context.edges){in::add_sweep_active_edge(context,edge.get());}
}
}

TEST(Clipper2NextTriangulationSweepDiagonalTests, SelectedProofIsConsumedByOneCreation) {
    in::triangulation_context context;prepare_square(context);
    auto* a=vertex(context,{0,0});auto* b=vertex(context,{10,10});
    ASSERT_NE(a,nullptr);ASSERT_NE(b,nullptr);
    const in::triangulation_sweep_front front{context,{20,-1}, clipper2next::internal::prepare_triangulation_predicate_domain(context)};
    auto proof=in::triangulation_sweep_diagonal::probe(front,a,b);ASSERT_TRUE(proof);
    auto* edge=in::create_sweep_loose_edge(std::move(*proof));ASSERT_NE(edge,nullptr);
    EXPECT_EQ(edge->first,a);EXPECT_EQ(edge->second,b);EXPECT_TRUE(edge->isActive);
    EXPECT_EQ(proof->first(),nullptr);EXPECT_EQ(proof->second(),nullptr);
    const auto count=context.edges.size();
    EXPECT_EQ(in::create_sweep_loose_edge(std::move(*proof)),nullptr);
    EXPECT_TRUE(context.internal_error);EXPECT_EQ(context.edges.size(),count);
}

TEST(Clipper2NextTriangulationSweepDiagonalTests, MoveTransfersOnlyLiveProof) {
    in::triangulation_context context;prepare_square(context);
    auto* a=vertex(context,{0,0});auto* b=vertex(context,{10,10});
    const in::triangulation_sweep_front front{context,{20,-1}, clipper2next::internal::prepare_triangulation_predicate_domain(context)};
    auto proof=in::triangulation_sweep_diagonal::probe(front,a,b);ASSERT_TRUE(proof);
    auto selected=std::move(*proof);
    EXPECT_EQ(proof->first(),nullptr);EXPECT_EQ(proof->second(),nullptr);
    EXPECT_EQ(selected.first(),a);EXPECT_EQ(selected.second(),b);
    EXPECT_EQ(selected.front().minimum(),front.minimum());
    EXPECT_EQ(&selected.front().context(),&context);
    ASSERT_NE(in::create_sweep_loose_edge(std::move(selected)),nullptr);
    EXPECT_EQ(selected.first(),nullptr);EXPECT_EQ(selected.second(),nullptr);
}

TEST(Clipper2NextTriangulationSweepDiagonalTests, RejectedProbeDoesNotCreateAnEdge) {
    in::triangulation_context context;prepare_square(context);
    const in::triangulation_sweep_front front{context,{20,-1}, clipper2next::internal::prepare_triangulation_predicate_domain(context)};
    const auto count=context.edges.size();
    EXPECT_FALSE(in::triangulation_sweep_diagonal::probe(front,vertex(context,{0,0}),vertex(context,{10,0})));
    EXPECT_EQ(context.edges.size(),count);EXPECT_TRUE(context.pending_delaunay.empty());
}

TEST(Clipper2NextTriangulationSweepDiagonalTests, PhaseMinimumIsAValueSnapshot) {
    in::triangulation_context context;prepare_square(context);
    auto proof=[&]{
        const in::triangulation_sweep_front front{context,{20,-1}, clipper2next::internal::prepare_triangulation_predicate_domain(context)};
        return in::triangulation_sweep_diagonal::probe(front,vertex(context,{0,0}),vertex(context,{10,10}));
    }();
    ASSERT_TRUE(proof);EXPECT_EQ(proof->front().minimum(),next::Point64(20,-1));
    ASSERT_NE(in::create_sweep_loose_edge(std::move(*proof)),nullptr);
}

TEST(Clipper2NextTriangulationSweepDiagonalTests, EventOwnershipKeepsTheOriginalCreationBoundary) {
    in::triangulation_context context;
    ASSERT_EQ(in::build_triangulation_boundary(context,{{{0,0},{10,0},{10,10},{0,10}},
        {{20,0},{30,0},{30,10},{20,10}}}),next::TriangulateResult::success);
    for(auto edge:context.edges){in::add_sweep_active_edge(context,edge.get());}
    const in::triangulation_sweep_front front{context,{40,-1}, clipper2next::internal::prepare_triangulation_predicate_domain(context)};
    auto proof=in::triangulation_sweep_diagonal::probe(front,vertex(context,{10,0}),vertex(context,{20,0}));
    ASSERT_TRUE(proof);
    const auto count=context.edges.size();
    EXPECT_EQ(in::create_sweep_loose_edge(std::move(*proof)),nullptr);
    EXPECT_FALSE(context.internal_error);EXPECT_EQ(context.edges.size(),count);
    EXPECT_EQ(proof->first(),nullptr);EXPECT_EQ(proof->second(),nullptr);
}
