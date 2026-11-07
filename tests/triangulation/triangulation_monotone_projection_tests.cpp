#include <gtest/gtest.h>
#include "triangulation/private/triangulation_monotone.h"
#include "triangulation/private/triangulation_simple.h"
#include "triangulation/private/triangulation_context.h"
#include "triangulation/private/triangulation_delaunay.h"
#include "triangulation/private/triangulation_mesh.h"
#include "triangulation/private/triangulation_executor.h"
#include "triangulation/private/triangulation_operation.h"
#include "clipper2next/triangulation/request.h"
#include "../../benchmarks/product/product_benchmark_fixtures.h"
#include <algorithm>

namespace next=clipper2next;
namespace in=next::internal;
TEST(Clipper2NextMonotoneProjectionTests, WiderDeclaredEllipseUsesHorizontalOrdering) {
    for(std::size_t n:{8,32,96}) {
        const auto path=next::benchmarks::product::make_triangulation_subject(n)[0];
        const auto domain=in::triangulation_predicate_domain::from_points(path.size(),[&](std::size_t i){return path[i];});
        in::monotone_workspace workspace;
        const auto layout=in::prepare_monotone_layout(path,domain,workspace);
        ASSERT_TRUE(layout);
        const auto physical=[&](std::size_t i){return path[layout->direction>0?i:path.size()-1U-i];};
        EXPECT_EQ(physical(layout->order.front().index).x,580);
        EXPECT_EQ(physical(layout->order.back().index).x,1420);
        EXPECT_EQ(layout->order.size(),n);
    }
}
TEST(Clipper2NextMonotoneProjectionTests, LongerUncertifiedAxisDoesNotLoseExistingVerticalDomain) {
    const next::Path64 original{{0,0},{100,0},{100,5},{70,6},{100,10},{0,10},{20,5}};
    for(std::size_t start=0;start<original.size();++start)for(bool reverse:{false,true}) {
        auto path=original;std::rotate(path.begin(),path.begin()+static_cast<std::ptrdiff_t>(start),path.end());
        if(reverse)std::reverse(path.begin(),path.end());
        const auto before=path;
        in::triangulation_context context;
        const auto domain=in::try_build_simple_triangulation_graph(context,{path});
        ASSERT_TRUE(domain);EXPECT_EQ(path,before);
        context.use_delaunay=false;
        ASSERT_TRUE(in::legalize_pending_delaunay_edges(context,*domain));
        EXPECT_EQ(in::validate_triangulation_mesh(context,*domain).size(),path.size()-2U);
    }
}

TEST(Clipper2NextMonotoneProjectionTests, CocircularProjectionPreservesTheGraphResult) {
    const next::Path64 original{{0, 0}, {6, 3}, {5, 5}, {-1, 2}};
    for (bool reverse : {false, true}) {
        auto path = original;
        if (reverse) { std::reverse(path.begin(), path.end()); }
        for (std::size_t i = 0; i < path.size(); ++i) {
            const next::Paths64 paths{path};
            auto status = next::TriangulateResult::fail;
            const auto expected = in::execute_triangulation(paths, true, status);
            ASSERT_EQ(status, next::TriangulateResult::success);
            ASSERT_EQ(expected.size(), 2U);
            {
                auto operation = in::triangulation_operation_lease{};
                next::Paths64 projected;
                ASSERT_TRUE(in::try_project_cocircular_quad(operation, path, projected));
                EXPECT_EQ(projected, expected);
            }
            const auto actual = next::triangulate(next::triangulation_request64{paths, true});
            EXPECT_EQ(actual.status, status);
            EXPECT_EQ(actual.triangles, expected);
            next::PathsD floating(1);
            for (const auto& p : path) { floating.front().emplace_back(p.x, p.y); }
            const auto scaled = next::triangulate(next::triangulation_requestd{floating, 0, true});
            EXPECT_EQ(scaled.status, status);
            ASSERT_EQ(scaled.triangles.size(), expected.size());
            for (std::size_t face = 0; face < expected.size(); ++face) {
                ASSERT_EQ(scaled.triangles[face].size(), expected[face].size());
                for (std::size_t point = 0; point < expected[face].size(); ++point) {
                    EXPECT_EQ(scaled.triangles[face][point].x, expected[face][point].x);
                    EXPECT_EQ(scaled.triangles[face][point].y, expected[face][point].y);
                }
            }
            std::rotate(path.begin(), path.begin() + 1, path.end());
        }
    }

    auto noncocircular = original;
    noncocircular.back().y += 2;
    auto operation = in::triangulation_operation_lease{};
    next::Paths64 output{original};
    EXPECT_FALSE(in::try_project_cocircular_quad(operation, noncocircular, output));
    EXPECT_EQ(output, next::Paths64{original});
}
