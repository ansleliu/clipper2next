#include <gtest/gtest.h>

#include "triangulation/private/triangulation_mesh.h"
#include "triangulation/private/triangulation_context.h"
#include "triangulation/private/triangulation_result_builder.h"
#include "clipper2next/geometry/algorithms.h"
#include <algorithm>

namespace next = clipper2next;
namespace in = next::internal;

namespace {
struct triangle_fixture {
    in::triangulation_edge* ab;
    in::triangulation_edge* bc;
    in::triangulation_edge* ca;
    in::triangulation_triangle* face;
};
auto triangle(in::triangulation_context& context, bool reverse_ab = false,
              next::Point64 third = {0, 10}) -> triangle_fixture {
    auto* a = in::create_triangulation_vertex(context, {0, 0});
    auto* b = in::create_triangulation_vertex(context, {10, 0});
    auto* c = in::create_triangulation_vertex(context, third);
    auto* ab = in::create_triangulation_edge(context, reverse_ab ? b : a, reverse_ab ? a : b,
                                             in::triangulation_edge_kind::ascend);
    auto* bc = in::create_triangulation_edge(context, b, c, in::triangulation_edge_kind::descend);
    auto* ca = in::create_triangulation_edge(context, c, a, in::triangulation_edge_kind::ascend);
    auto* face = in::create_triangulation_triangle(context, bc, ca, ab);
    for (auto* side : {ab, bc, ca}) { side->triA = face; }
    return {ab, bc, ca, face};
}
}  // namespace

TEST(Clipper2NextTriangulationMeshTests, CompletedViewOwnsNegativeCanonicalOrientation) {
    in::triangulation_context context;
    const auto source = triangle(context);
    const auto view = in::validate_triangulation_mesh(context, clipper2next::internal::prepare_triangulation_predicate_domain(context));
    ASSERT_EQ(view.size(), 1U);
    EXPECT_LT(source.face->orientation, 0);
    const auto result = in::build_triangulation_result(view);
    ASSERT_EQ(result.size(), 1U);
    EXPECT_GT(next::area(result[0]), 0.0);

    const auto sides = source.face->edges;
    std::array<unsigned, 3> order{0U, 1U, 2U};
    do {
        for (std::size_t i = 0; i < order.size(); ++i) { source.face->edges[i] = sides[order[i]]; }
        const auto permuted = in::validate_triangulation_mesh(
            context, in::prepare_triangulation_predicate_domain(context));
        EXPECT_GT(next::area(in::build_triangulation_result(permuted).front()), 0.0);
    } while (std::next_permutation(order.begin(), order.end()));
}

TEST(Clipper2NextTriangulationMeshTests, ZeroAreaIsRejectedBeforeAnyOutputCandidate) {
    in::triangulation_context context;
    (void)triangle(context, false, {5, 0});
    EXPECT_THROW((void)in::validate_triangulation_mesh(context, clipper2next::internal::prepare_triangulation_predicate_domain(context)), next::clipper_error);
}

TEST(Clipper2NextTriangulationMeshTests, EveryListedFaceMustBeReciprocallyAssociated) {
    {
        in::triangulation_context context;
        const auto valid = triangle(context);
        (void)in::create_triangulation_triangle(context, valid.ab, valid.bc, valid.ca);
        EXPECT_THROW((void)in::validate_triangulation_mesh(context, clipper2next::internal::prepare_triangulation_predicate_domain(context)), next::clipper_error);
    }
    {
        in::triangulation_context context;
        const auto valid = triangle(context);
        auto* unlisted = in::create_triangulation_edge(
            context, valid.bc->first.get(), valid.bc->second.get(), valid.bc->kind);
        unlisted->triA = valid.face;
        EXPECT_THROW((void)in::validate_triangulation_mesh(context, clipper2next::internal::prepare_triangulation_predicate_domain(context)), next::clipper_error);
    }
}

TEST(Clipper2NextTriangulationMeshTests, EveryTriangleMustCloseThreeDistinctVertexPairs) {
    for (const bool distinct_object : {false, true}) {
        in::triangulation_context context;
        const auto source = triangle(context);
        if (distinct_object) {
            auto* duplicate = in::create_triangulation_edge(
                context, source.bc->second.get(), source.bc->first.get(), source.bc->kind);
            duplicate->triA = source.face;
            source.face->edges[2] = duplicate;
            const auto unused = std::find(context.edges.begin(), context.edges.end(), source.ab);
            ASSERT_NE(unused, context.edges.end());
            context.edges.erase(unused);
        } else { source.face->edges[2] = source.face->edges[0]; }
        EXPECT_THROW((void)in::validate_triangulation_mesh(
            context, in::prepare_triangulation_predicate_domain(context)), next::clipper_error);
    }
}

TEST(Clipper2NextTriangulationMeshTests, ConstraintDirectionIsPartOfCompletedMesh) {
    in::triangulation_context context;
    (void)triangle(context, true);
    EXPECT_THROW((void)in::validate_triangulation_mesh(context, clipper2next::internal::prepare_triangulation_predicate_domain(context)), next::clipper_error);
}

TEST(Clipper2NextTriangulationMeshTests, TwoInteriorOwnersCannotOccupyTheSameSide) {
    in::triangulation_context context;
    auto* a = in::create_triangulation_vertex(context, {0,0});
    auto* b = in::create_triangulation_vertex(context, {10,0});
    auto* c = in::create_triangulation_vertex(context, {0,10});
    auto* d = in::create_triangulation_vertex(context, {1,9});
    auto* ab = in::create_triangulation_edge(context, a, b);
    auto* bc = in::create_triangulation_edge(context, b, c, in::triangulation_edge_kind::ascend);
    auto* ca = in::create_triangulation_edge(context, c, a, in::triangulation_edge_kind::ascend);
    auto* bd = in::create_triangulation_edge(context, b, d, in::triangulation_edge_kind::ascend);
    auto* da = in::create_triangulation_edge(context, d, a, in::triangulation_edge_kind::ascend);
    auto* first = in::create_triangulation_triangle(context, ab, bc, ca);
    auto* second = in::create_triangulation_triangle(context, ab, bd, da);
    ab->triA=first;ab->triB=second;bc->triA=first;ca->triA=first;bd->triA=second;da->triA=second;
    EXPECT_THROW((void)in::validate_triangulation_mesh(context, clipper2next::internal::prepare_triangulation_predicate_domain(context)), next::clipper_error);
}

TEST(Clipper2NextTriangulationMeshTests, EmptyViewClearsTheOwningOutput) {
    const in::triangulation_mesh_view view;
    next::Paths64 output{{{1,1},{2,1},{1,2}}};
    in::build_triangulation_result_into(view, output);
    EXPECT_TRUE(output.empty());
}
