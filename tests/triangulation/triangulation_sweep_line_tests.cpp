#include "triangulation/private/triangulation_boundary_sector.h"
#include <gtest/gtest.h>

#include "triangulation/private/triangulation_boundary.h"
#include "triangulation/private/triangulation_context.h"
#include "triangulation/private/triangulation_boundary_events.h"
#include "triangulation/private/triangulation_sweep_line.h"
#include "triangulation/private/triangulation_sweep_line.h"
#include "triangulation/private/triangulation_topology.h"

#include <algorithm>
#include <array>

namespace next = clipper2next;
namespace in = next::internal;

namespace {
using face_sides = std::array<in::triangulation_edge*, 3>;
using face_vertices = std::array<in::triangulation_vertex*, 3>;

auto make_manual_face(in::triangulation_context& context, unsigned directions = 0,
                      std::array<next::Point64, 3> points = {{{0, 0}, {12, 0}, {0, 8}}})
    -> face_sides {
    face_vertices vertices;
    for (std::size_t i = 0; i != 3; ++i) {
        vertices[i] = in::create_triangulation_vertex(context, points[i]);
    }
    face_sides sides;
    for (std::size_t i = 0; i != 3; ++i) {
        auto* a = vertices[i];
        auto* b = vertices[(i + 1) % 3];
        if (directions & (1U << i)) { std::swap(a, b); }
        sides[i] = in::create_triangulation_edge(context, a, b);
        in::add_sweep_active_edge(context, sides[i]);
    }
    return sides;
}

auto set_owner(in::triangulation_edge& edge, in::triangulation_vertex* vertex,
               std::size_t owner) -> void {
    ASSERT_TRUE(edge.first == vertex || edge.second == vertex);
    if (edge.first == vertex) { edge.first_event = owner; }
    else { edge.second_event = owner; }
}

auto expect_unpublished(in::triangulation_context& context, const face_sides& sides) -> void {
    EXPECT_EQ(in::create_sweep_triangle(context, sides[0], sides[1], sides[2], clipper2next::internal::prepare_triangulation_predicate_domain(context)), nullptr);
    EXPECT_TRUE(context.internal_error);
    EXPECT_TRUE(context.triangles.empty());
    for (auto* side : sides) {
        EXPECT_EQ(side->triA, nullptr);
        EXPECT_EQ(side->triB, nullptr);
    }
}
}  // namespace

TEST(Clipper2NextTriangulationSweepLineTests, ActiveEdgeInsertionIsIdempotent) {
    next::internal::triangulation_context context;
    auto* first = next::internal::create_triangulation_vertex(context, {0, 10});
    auto* second = next::internal::create_triangulation_vertex(context, {10, 0});
    auto* edge = next::internal::create_triangulation_edge(
        context, first, second, next::internal::triangulation_edge_kind::ascend);

    next::internal::add_sweep_active_edge(context, edge);
    next::internal::add_sweep_active_edge(context, edge);

    EXPECT_TRUE(edge->isActive);
    EXPECT_EQ(context.first_active, edge);
    EXPECT_EQ(edge->nextE, nullptr);
}

TEST(Clipper2NextTriangulationSweepLineTests, ActiveEdgeRemovalRelinksNeighborEdges) {
    next::internal::triangulation_context context;
    auto* a = next::internal::create_triangulation_vertex(context, {0, 10});
    auto* b = next::internal::create_triangulation_vertex(context, {10, 0});
    auto* c = next::internal::create_triangulation_vertex(context, {20, 10});
    auto* d = next::internal::create_triangulation_vertex(context, {30, 0});
    auto* first = next::internal::create_triangulation_edge(context, a, b);
    auto* second = next::internal::create_triangulation_edge(context, c, d);

    next::internal::add_sweep_active_edge(context, first);
    next::internal::add_sweep_active_edge(context, second);
    next::internal::remove_sweep_active_edge(context, second);

    EXPECT_FALSE(second->isActive);
    EXPECT_EQ(context.first_active, first);
    EXPECT_EQ(first->prevE, nullptr);
}

TEST(Clipper2NextTriangulationSweepLineTests, SweepTriangulatesHorizontalSquare) {
    next::internal::triangulation_context context;
    const next::Paths64 paths{
        next::Path64{{0, 0}, {40, 0}, {40, 40}, {0, 40}},
    };

    ASSERT_EQ(next::internal::build_triangulation_boundary(context, paths),
              next::TriangulateResult::success);

    EXPECT_EQ(next::internal::run_triangulation_sweep(context, clipper2next::internal::prepare_triangulation_predicate_domain(context)), next::TriangulateResult::success);
    EXPECT_FALSE(context.triangles.empty());
}

TEST(Clipper2NextTriangulationSweepLineTests, SweepHandlesDuplicateXVerticalBounds) {
    next::internal::triangulation_context context;
    const next::Paths64 paths{
        next::Path64{{0, 0}, {0, 20}, {10, 30}, {20, 20}, {20, 0}},
    };

    ASSERT_EQ(next::internal::build_triangulation_boundary(context, paths),
              next::TriangulateResult::success);

    EXPECT_EQ(next::internal::run_triangulation_sweep(context, clipper2next::internal::prepare_triangulation_predicate_domain(context)), next::TriangulateResult::success);
    EXPECT_FALSE(context.triangles.empty());
}

TEST(Clipper2NextTriangulationSweepLineTests, CreatorPreservesEverySideOrderAndDirection) {
    for (unsigned directions = 0; directions != 8; ++directions) {
        std::array<unsigned, 3> order{0, 1, 2};
        do {
            SCOPED_TRACE(::testing::Message() << directions << '/' << order[0] << order[1] << order[2]);
            in::triangulation_context context;
            const auto sides = make_manual_face(context, directions);
            auto* triangle = in::create_sweep_triangle(context, sides[order[0]], sides[order[1]], sides[order[2]], clipper2next::internal::prepare_triangulation_predicate_domain(context));
            ASSERT_NE(triangle, nullptr);
            ASSERT_FALSE(context.internal_error);
            ASSERT_EQ(context.triangles.size(), 1U);
            for (std::size_t i = 0; i != 3; ++i) {
                EXPECT_EQ(triangle->edges[i], sides[order[i]]);
                EXPECT_EQ(sides[i]->triA, triangle);
                EXPECT_EQ(sides[i]->triB, nullptr);
            }
        } while (std::next_permutation(order.begin(), order.end()));
    }
}

TEST(Clipper2NextTriangulationSweepLineTests, CreatorRejectsMalformedSideSetsBeforePublication) {
    for (unsigned kind = 0; kind != 5; ++kind) {
        SCOPED_TRACE(kind);
        in::triangulation_context context;
        auto sides = make_manual_face(context);
        if (kind == 0) { sides[2] = sides[0]; }
        if (kind == 1) {
            auto* extra = in::create_triangulation_vertex(context, {20, 20});
            sides[2] = in::create_triangulation_edge(context, sides[1]->second.get(), extra);
        }
        if (kind == 2) {
            sides[2] = in::create_triangulation_edge(context, sides[0]->second.get(), sides[0]->first.get());
        }
        if (kind == 3) {
            sides[2] = in::create_triangulation_edge(context, sides[0]->first.get(), sides[0]->first.get());
        }
        if (kind == 4) { sides[1]->second->pt = sides[0]->first->pt; }
        expect_unpublished(context, sides);
    }
    in::triangulation_context collinear;
    auto sides = make_manual_face(collinear, 0, {{{0, 0}, {4, 0}, {8, 0}}});
    expect_unpublished(collinear, sides);
}

TEST(Clipper2NextTriangulationSweepLineTests, CreatorKeepsPerCornerOptionalOwnerSemantics) {
    for (unsigned present = 0; present != 8; ++present) {
        SCOPED_TRACE(present);
        in::triangulation_context context;
        ASSERT_EQ(in::build_triangulation_boundary(context, {{{0, 0}, {12, 0}, {0, 8}}}),
                  next::TriangulateResult::success);
        ASSERT_EQ(context.edges.size(), 3U);
        face_sides sides{context.edges[0].get(), context.edges[1].get(), context.edges[2].get()};
        for (std::size_t i = 0; i != context.vertices.size(); ++i) {
            if (present & (1U << i)) { continue; }
            auto* vertex = context.vertices[i].get();
            for (auto* side : sides) {
                if (side->first == vertex || side->second == vertex) {
                    set_owner(*side, vertex, in::no_boundary_event);
                }
            }
        }
        for (auto* side : sides) { in::add_sweep_active_edge(context, side); }
        ASSERT_NE(in::create_sweep_triangle(context, sides[0], sides[1], sides[2], clipper2next::internal::prepare_triangulation_predicate_domain(context)), nullptr);
        EXPECT_FALSE(context.internal_error);
    }
}

TEST(Clipper2NextTriangulationSweepLineTests, CreatorRejectsEveryCurrentCornerOwnerMismatch) {
    for (unsigned corner = 0; corner != 3; ++corner) {
        SCOPED_TRACE(corner);
        in::triangulation_context context;
        ASSERT_EQ(in::build_triangulation_boundary(context, {{{0, 0}, {12, 0}, {0, 8}}}),
                  next::TriangulateResult::success);
        face_sides sides{context.edges[0].get(), context.edges[1].get(), context.edges[2].get()};
        auto* vertex = context.vertices[corner].get();
        for (auto* side : sides) {
            if (side->first == vertex || side->second == vertex) {
                set_owner(*side, vertex, in::no_boundary_event);
                break;
            }
        }
        expect_unpublished(context, sides);
    }
}

TEST(Clipper2NextTriangulationSweepLineTests, CreatorChecksPresentExcludedCornerWhenOthersAreAbsent) {
    for (unsigned rotation = 0; rotation != 3; ++rotation) {
        SCOPED_TRACE(rotation);
        in::triangulation_context context;
        ASSERT_EQ(in::build_triangulation_boundary(context,
            {{{0, 0}, {10, 0}, {10, 10}, {0, 10}}, {{2, 2}, {2, 8}, {8, 8}, {8, 2}}}),
            next::TriangulateResult::success);
        face_vertices vertices{};
        for (auto vertex : context.vertices) {
            if (vertex->pt == next::Point64(2, 2)) { vertices[0] = vertex.get(); }
            if (vertex->pt == next::Point64(2, 8)) { vertices[1] = vertex.get(); }
            if (vertex->pt == next::Point64(8, 2)) { vertices[2] = vertex.get(); }
        }
        ASSERT_NE(vertices[0], nullptr);
        const auto owner = vertices[0]->event_begin;
        ASSERT_FALSE(context.boundary_events[owner].sector.contains_triangle(vertices[1]->pt, vertices[2]->pt));
        face_sides sides{};
        for (std::size_t i = 0; i != 3; ++i) {
            sides[i] = in::create_triangulation_edge(context, vertices[i], vertices[(i + 1) % 3]);
            if (sides[i]->first == vertices[0] || sides[i]->second == vertices[0]) {
                set_owner(*sides[i], vertices[0], owner);
            }
        }
        face_sides ordered{sides[rotation], sides[(rotation + 1) % 3], sides[(rotation + 2) % 3]};
        expect_unpublished(context, ordered);
    }
}

TEST(Clipper2NextTriangulationSweepLineTests, CreatorPreservesNullCompletedAndExistingErrorGuards) {
    in::triangulation_context context;
    auto sides = make_manual_face(context);
    EXPECT_EQ(in::create_sweep_triangle(context, nullptr, sides[1], sides[2], clipper2next::internal::prepare_triangulation_predicate_domain(context)), nullptr);
    EXPECT_TRUE(context.internal_error);
    EXPECT_TRUE(context.triangles.empty());
    EXPECT_EQ(in::create_sweep_triangle(context, sides[0], sides[1], sides[2], clipper2next::internal::prepare_triangulation_predicate_domain(context)), nullptr);
    EXPECT_TRUE(context.triangles.empty());

    in::triangulation_context completed;
    auto occupied = make_manual_face(completed);
    auto* previous = in::create_triangulation_triangle(completed, occupied[0], occupied[1], occupied[2]);
    occupied[0]->kind = in::triangulation_edge_kind::ascend;
    occupied[0]->triA = previous;
    EXPECT_EQ(in::create_sweep_triangle(completed, occupied[0], occupied[1], occupied[2], clipper2next::internal::prepare_triangulation_predicate_domain(completed)), nullptr);
    EXPECT_TRUE(completed.internal_error);
    EXPECT_EQ(completed.triangles.size(), 1U);
    EXPECT_EQ(occupied[0]->triA, previous);
    EXPECT_EQ(occupied[1]->triA, nullptr);
}
