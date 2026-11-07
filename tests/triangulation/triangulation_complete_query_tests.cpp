#include <gtest/gtest.h>
#include "triangulation/private/triangulation_context.h"
#include "triangulation/private/triangulation_sweep_line.h"
#include <array>

namespace next=clipper2next;
namespace in=next::internal;
namespace {
struct Sample {
    next::Point64 a,b,c,d;
    bool visible;
};
auto check(const Sample& sample, bool share_first=false, bool share_second=false,
           in::triangulation_edge_kind kind=in::triangulation_edge_kind::loose) -> void {
    in::triangulation_context context;
    auto* a=in::create_triangulation_vertex(context,sample.a);
    auto* b=in::create_triangulation_vertex(context,sample.b);
    auto* c=share_first ? a : in::create_triangulation_vertex(context,sample.c);
    auto* d=share_second ? b : in::create_triangulation_vertex(context,sample.d);
    auto* edge=in::create_triangulation_edge(context,c,d,kind);
    in::add_sweep_active_edge(context,edge);
    const auto vertex_count=context.vertices.size(),edge_count=context.edges.size();
    const auto minimum=in::sweep_before(sample.a,sample.b) ? sample.b : sample.a;
    const in::triangulation_sweep_front front{context,minimum, clipper2next::internal::prepare_triangulation_predicate_domain(context)};
    const auto proof=in::triangulation_sweep_diagonal::probe(front,a,b);
    EXPECT_EQ(proof.has_value(),sample.visible);
    EXPECT_EQ(context.vertices.size(),vertex_count);
    EXPECT_EQ(context.edges.size(),edge_count);
    EXPECT_EQ(context.first_active,edge);
    EXPECT_TRUE(context.triangles.empty());
    EXPECT_FALSE(context.internal_error);
}
}

TEST(Clipper2NextTriangulationCompleteQueryTests, CompleteCrossingAndContactPolicies) {
    for(const auto& sample : std::array{
        Sample{{0,0},{10,10},{0,10},{10,0},false},
        Sample{{0,0},{10,10},{5,5},{5,9},false},
        Sample{{0,0},{10,0},{2,0},{5,0},false},
        Sample{{0,0},{10,0},{-5,0},{15,0},true},
        Sample{{0,0},{10,10},{20,0},{30,10},true},
        Sample{{0,0},{10,10},{10,0},{20,0},true},
        Sample{{0,0},{10,10},{-1,-1},{11,11},true},
        Sample{{0,0},{0,0},{0,1},{1,0},false}}) { check(sample); }
}

TEST(Clipper2NextTriangulationCompleteQueryTests, IdentityRemainsSeparateFromSameCoordinateContact) {
    check({{0,0},{10,0},{0,0},{-4,2},false});
    check({{0,0},{10,0},{0,0},{-4,2},true},true);
    check({{0,0},{10,0},{0,0},{10,0},true},true,true);
    check({{0,0},{10,0},{0,0},{10,0},false},true,true,in::triangulation_edge_kind::ascend);
}

TEST(Clipper2NextTriangulationCompleteQueryTests, FullAdmittedRangeAndExtremeTranslationKeepExactPredicates) {
    constexpr auto m=next::MAX_COORD;
    constexpr int64_t wide=int64_t{1}<<32;
    check({{0,0},{wide,1},{1,0},{1,2},false});
    check({{-m,-m},{m,m},{-m,m},{m,-m},false});
    check({{-m,-m},{m,m},{-m,m},{-m+1,m},true});
    check({{m-10,m-10},{m,m},{m-5,m-4},{m-4,m-3},true});
    check({{m-10,m-10},{m,m},{m-5,m-5},{m-5,m-4},false});
}
