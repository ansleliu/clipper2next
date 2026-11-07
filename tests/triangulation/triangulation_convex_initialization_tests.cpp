#include <gtest/gtest.h>
#include "triangulation/private/triangulation_simple.h"
#include "triangulation/private/triangulation_context.h"
#include "triangulation/private/triangulation_delaunay.h"
#include "triangulation/private/triangulation_mesh.h"
#include "../../benchmarks/product/product_benchmark_fixtures.h"
#include <algorithm>
#include <array>

namespace next=clipper2next;
namespace in=next::internal;
namespace {
auto diagonals(const in::triangulation_context& context) {
    std::vector<std::array<int64_t,4>> result;
    for(auto edge:context.edges) {
        if(edge->kind!=in::triangulation_edge_kind::loose)continue;
        auto a=edge->first->pt,b=edge->second->pt;
        if(std::pair{b.x,b.y}<std::pair{a.x,a.y})std::swap(a,b);
        result.push_back({a.x,a.y,b.x,b.y});
    }
    std::sort(result.begin(),result.end());return result;
}
void check(const next::Paths64& paths) {
    in::triangulation_context context;
    const auto domain=in::try_build_simple_triangulation_graph(context,paths);
    ASSERT_TRUE(domain);
    const auto before=diagonals(context);
    EXPECT_EQ(before.size(),paths.front().size()-3U);
    ASSERT_TRUE(in::legalize_pending_delaunay_edges(context,*domain));
    EXPECT_EQ(diagonals(context),before);
    EXPECT_EQ(in::validate_triangulation_mesh(context,*domain).size(),paths.front().size()-2U);
}
}
TEST(Clipper2NextConvexInitializationTests, DeclaredEllipseFixturesNeedNoFollowingDiagonalReplacement) {
    for(std::size_t n:{8,32})check(next::benchmarks::product::make_triangulation_subject(n));
}
TEST(Clipper2NextConvexInitializationTests, DeclaredParabolaFamiliesKeepTheirInitiallyLegalDiagonals) {
    for(std::size_t n:{4,8,32,128}) {
        next::Path64 path;
        for(std::size_t i=0;i<n;++i){const auto x=static_cast<int64_t>(i);path.push_back({x,x*x});}
        check({path});std::reverse(path.begin(),path.end());
        constexpr auto shift=int64_t{1}<<59;
        for(auto& p:path){p.x+=shift;p.y-=shift;}
        check({path});
    }
}
