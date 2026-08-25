#include <gtest/gtest.h>

#include "triangulation/private/triangulation_context.h"
#include "triangulation/private/triangulation_delaunay.h"

#include <memory>

namespace next = clipper2next;
namespace {

struct QuadrilateralFixture {
    next::internal::triangulation_context context;
    next::internal::triangulation_vertex* a = nullptr;
    next::internal::triangulation_vertex* b = nullptr;
    next::internal::triangulation_vertex* c = nullptr;
    next::internal::triangulation_vertex* d = nullptr;
    next::internal::triangulation_edge* shared = nullptr;
};

auto make_quadrilateral(next::Point64 fourth) -> std::unique_ptr<QuadrilateralFixture> {
    auto fixture = std::make_unique<QuadrilateralFixture>();
    fixture->a = next::internal::create_triangulation_vertex(fixture->context, {0, 0});
    fixture->b = next::internal::create_triangulation_vertex(fixture->context, {10, 0});
    fixture->c = next::internal::create_triangulation_vertex(fixture->context, {0, 10});
    fixture->d = next::internal::create_triangulation_vertex(fixture->context, fourth);

    fixture->shared =
        next::internal::create_triangulation_edge(fixture->context, fixture->a, fixture->b);
    auto* ac = next::internal::create_triangulation_edge(fixture->context, fixture->a, fixture->c);
    auto* cb = next::internal::create_triangulation_edge(fixture->context, fixture->c, fixture->b);
    auto* bd = next::internal::create_triangulation_edge(fixture->context, fixture->b, fixture->d);
    auto* da = next::internal::create_triangulation_edge(fixture->context, fixture->d, fixture->a);

    auto* first_triangle =
        next::internal::create_triangulation_triangle(fixture->context, fixture->shared, ac, cb);
    auto* second_triangle =
        next::internal::create_triangulation_triangle(fixture->context, fixture->shared, bd, da);

    fixture->shared->triA = first_triangle;
    fixture->shared->triB = second_triangle;
    ac->triA = first_triangle;
    cb->triA = first_triangle;
    bd->triA = second_triangle;
    da->triA = second_triangle;
    return fixture;
}

}  // namespace

TEST(Clipper2NextTriangulationDelaunayTests, LegalizationSkipsBoundaryEdges) {
    next::internal::triangulation_context context;
    auto* first = next::internal::create_triangulation_vertex(context, {0, 0});
    auto* second = next::internal::create_triangulation_vertex(context, {10, 0});
    auto* edge = next::internal::create_triangulation_edge(context, first, second);

    next::internal::force_triangulation_edge_legal(context, edge);

    EXPECT_EQ(edge->vL, first);
    EXPECT_EQ(edge->vR, second);
}

TEST(Clipper2NextTriangulationDelaunayTests, PendingStackIsDrained) {
    next::internal::triangulation_context context;
    auto* first = next::internal::create_triangulation_vertex(context, {0, 0});
    auto* second = next::internal::create_triangulation_vertex(context, {10, 0});
    auto* edge = next::internal::create_triangulation_edge(context, first, second);
    context.pending_delaunay.push_back(edge);

    next::internal::legalize_pending_delaunay_edges(context);

    EXPECT_TRUE(context.pending_delaunay.empty());
}

TEST(Clipper2NextTriangulationDelaunayTests, IllegalConvexQuadrilateralFlipsSharedEdge) {
    auto fixture = make_quadrilateral({5, -1});

    next::internal::force_triangulation_edge_legal(fixture->context, fixture->shared);

    EXPECT_EQ(fixture->shared->vL, fixture->c);
    EXPECT_EQ(fixture->shared->vR, fixture->d);
}

TEST(Clipper2NextTriangulationDelaunayTests, LegalConvexQuadrilateralKeepsSharedEdge) {
    auto fixture = make_quadrilateral({5, -5});

    next::internal::force_triangulation_edge_legal(fixture->context, fixture->shared);

    EXPECT_EQ(fixture->shared->vL, fixture->a);
    EXPECT_EQ(fixture->shared->vR, fixture->b);
}

TEST(Clipper2NextTriangulationDelaunayTests, DegenerateQuadrilateralKeepsSharedEdge) {
    auto fixture = make_quadrilateral({5, -1});
    fixture->c->pt = {5, 0};

    next::internal::force_triangulation_edge_legal(fixture->context, fixture->shared);

    EXPECT_EQ(fixture->shared->vL, fixture->a);
    EXPECT_EQ(fixture->shared->vR, fixture->b);
}
