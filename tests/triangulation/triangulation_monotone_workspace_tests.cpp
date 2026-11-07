#include <gtest/gtest.h>
#include "triangulation/private/triangulation_context.h"
#include "triangulation/private/triangulation_simple.h"
#include "triangulation/private/triangulation_delaunay.h"
#include "triangulation/private/triangulation_mesh.h"
#include "../../benchmarks/product/product_benchmark_fixtures.h"
#include <algorithm>

namespace next=clipper2next;
namespace in=next::internal;
TEST(Clipper2NextMonotoneWorkspaceTests, RepeatedContextClearPreservesGeometryAcrossSizesAndDirections) {
    in::triangulation_context context;
    for(int round=0;round!=3;++round)for(std::size_t size:{96,8,32,4})for(bool reverse:{false,true}) {
        auto paths=next::benchmarks::product::make_triangulation_subject(size);
        for(auto& point:paths[0]){point.x+=round*12345;point.y-=round*6789;}
        if(reverse)std::reverse(paths[0].begin(),paths[0].end());
        context.clear();context.use_delaunay=false;
        const auto domain=in::try_build_simple_triangulation_graph(context,paths);
        ASSERT_TRUE(domain);ASSERT_TRUE(in::legalize_pending_delaunay_edges(context,*domain));
        const auto mesh=in::validate_triangulation_mesh(context,*domain);
        EXPECT_EQ(mesh.size(),size-2U);
        EXPECT_EQ(context.vertices.size(),size);
    }
}
TEST(Clipper2NextMonotoneWorkspaceTests, ContextReleaseAllowsFreshGeometryAfterPartialPreparation) {
    in::triangulation_context context;
    const next::Paths64 crossed{{{0,0},{10,10},{0,10},{10,0}}};
    EXPECT_FALSE(in::try_build_simple_triangulation_graph(context,crossed));
    context.release();context.release();
    for(std::size_t size:{8,96,32}) {
        context.clear();context.use_delaunay=false;
        const auto paths=next::benchmarks::product::make_triangulation_subject(size);
        const auto domain=in::try_build_simple_triangulation_graph(context,paths);
        ASSERT_TRUE(domain);ASSERT_TRUE(in::legalize_pending_delaunay_edges(context,*domain));
        EXPECT_EQ(in::validate_triangulation_mesh(context,*domain).size(),size-2U);
        context.release();
    }
}
