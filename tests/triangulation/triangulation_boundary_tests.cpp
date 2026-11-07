#include "triangulation/private/triangulation_boundary_sector.h"
#include <gtest/gtest.h>

#include <limits>
#include <type_traits>

#include "triangulation/private/triangulation_boundary.h"
#include "triangulation/private/triangulation_boundary_events.h"
#include "triangulation/private/triangulation_context.h"
#include "triangulation/private/triangulation_path_builder.h"
#include "triangulation/private/triangulation_sweep_line.h"

namespace next = clipper2next;

static_assert(std::is_same_v<decltype(next::internal::boundary_winding(next::Path64{}, {}, {}, {})),
                             int64_t>);

TEST(Clipper2NextTriangulationBoundaryTests, CreatesOuterAndHoleBoundaries) {
    next::internal::triangulation_context context;
    const next::Paths64 paths{
        next::Path64{{0, 0}, {100, 0}, {100, 100}, {0, 100}},
        next::Path64{{25, 25}, {25, 75}, {75, 75}, {75, 25}},
    };

    EXPECT_EQ(next::internal::build_triangulation_boundary(context, paths),
              next::TriangulateResult::success);

    EXPECT_GE(context.vertices.size(), 8U);
    EXPECT_GE(context.edges.size(), 8U);
    EXPECT_FALSE(context.boundary_events.empty());
}

TEST(Clipper2NextTriangulationBoundaryTests, RejectsRepeatedPointDegenerateInput) {
    next::internal::triangulation_context context;

    EXPECT_FALSE(context.source_paths.append({next::Path64{{0, 0}, {0, 0}, {0, 0}, {0, 0}}}));

    EXPECT_TRUE(context.source_paths.empty());
}

TEST(Clipper2NextTriangulationBoundaryTests, TouchHasIndependentCrossRingSectors) {
    next::internal::triangulation_context context;
    const next::Paths64 paths{{{0, 0}, {10, 0}, {10, 10}, {0, 10}}, {{0, 5}, {2, 7}, {2, 3}}};
    ASSERT_EQ(next::internal::build_triangulation_boundary(context, paths),
              next::TriangulateResult::success);
    const next::internal::triangulation_vertex* touch = nullptr;
    for (auto vertex : context.vertices) {
        if (vertex->pt == next::Point64(0, 5)) { touch = vertex.get(); }
    }
    ASSERT_NE(touch, nullptr);
    ASSERT_EQ(touch->event_end - touch->event_begin, 2U);
    for (auto id = touch->event_begin; id < touch->event_end; ++id) {
        const auto& event = context.boundary_events[id];
        if (event.incoming->first->pt == next::Point64(0, 10)) {
            EXPECT_EQ(event.outgoing->second->pt, next::Point64(2, 7));
            EXPECT_EQ(event.kind, next::internal::boundary_event_kind::close);
        } else {
            EXPECT_EQ(event.incoming->first->pt, next::Point64(2, 3));
            EXPECT_EQ(event.outgoing->second->pt, next::Point64(0, 0));
            EXPECT_EQ(event.kind, next::internal::boundary_event_kind::open);
        }
    }
    // A realistic ownership-collapse mutation must fail the actual sweep.
    context.boundary_events[touch->event_begin + 1].outgoing->first_event = touch->event_begin;
    EXPECT_EQ(next::internal::run_triangulation_sweep(context, clipper2next::internal::prepare_triangulation_predicate_domain(context)), next::TriangulateResult::fail);
}


TEST(Clipper2NextTriangulationBoundaryTests, ReflexBoundaryRaysCannotBoundTheExcludedWedge) {
    next::internal::triangulation_context context;
    const next::Paths64 paths{{{0, 0}, {10, 0}, {10, 10}, {0, 10}},
                              {{2, 2}, {2, 8}, {8, 8}, {8, 2}}};
    ASSERT_EQ(next::internal::build_triangulation_boundary(context, paths),
              next::TriangulateResult::success);
    for (const auto& event : context.boundary_events) {
        if (event.vertex->pt != next::Point64(2, 2)) { continue; }
        ASSERT_TRUE(event.reflex);
        EXPECT_FALSE(event.sector.contains_triangle({2, 8}, {8, 2}));
        EXPECT_FALSE(event.sector.contains_triangle({8, 2}, {2, 8}));
        EXPECT_TRUE(event.sector.contains_triangle({0, 0}, {0, 10}));
    }
}
TEST(Clipper2NextTriangulationBoundaryTests, SectorWedgesRespectEndpointsAndGlobalReversal) {
    struct wedge_case {
        next::Point64 pivot;
        next::Point64 a;
        next::Point64 b;
        bool allowed;
    };
    const std::vector<wedge_case> cases{
        {{0, 0}, {10, 0}, {0, 10}, true},
        {{0, 0}, {5, 5}, {10, 0}, true},
        {{0, 0}, {-1, 0}, {1, 1}, false},
        {{2, 2}, {2, 8}, {8, 2}, false},
        {{2, 2}, {2, 8}, {0, 2}, true},
        {{2, 2}, {2, 0}, {8, 2}, true},
        {{2, 2}, {3, 3}, {2, 8}, false},
        {{2, 2}, {1, 12}, {12, 1}, false},
        {{5, 0}, {6, 0}, {5, 1}, true},
        {{5, 0}, {4, 0}, {5, 1}, true},
        {{5, 0}, {5, -1}, {6, 0}, false},
        {{5, 0}, {4, 0}, {6, 0}, false},
    };
    for (bool reverse : {false, true}) {
        next::internal::triangulation_context context;
        next::Paths64 paths{{{0, 0}, {5, 0}, {10, 0}, {10, 10}, {0, 10}},
                            {{2, 2}, {2, 8}, {8, 8}, {8, 2}}};
        if (reverse) {
            for (auto& path : paths) { std::reverse(path.begin(), path.end()); }
        }
        ASSERT_EQ(next::internal::build_triangulation_boundary(context, paths),
                  next::TriangulateResult::success);
        for (const auto& test : cases) {
            const auto event =
                std::find_if(context.boundary_events.begin(),
                             context.boundary_events.end(),
                             [&](const auto& value) { return value.vertex->pt == test.pivot; });
            ASSERT_NE(event, context.boundary_events.end());
            EXPECT_EQ(event->sector.contains_triangle(test.a, test.b), test.allowed);
            EXPECT_EQ(event->sector.contains_triangle(test.b, test.a), test.allowed);
        }
    }
}

TEST(Clipper2NextTriangulationBoundaryTests, RepeatedClosedWalkRetainsSignedWinding) {
    const next::Path64 square{{-8, -8}, {8, -8}, {8, 8}, {-8, 8}};
    next::Path64 walk;
    for (int repeat = 0; repeat < 257; ++repeat) {
        walk.insert(walk.end(), square.begin(), square.end());
    }
    struct probe_case {
        next::Point64 a;
        next::Point64 b;
        next::internal::boundary_ray displacement;
        int64_t winding;
    };
    const std::vector<probe_case> probes{
        {{-1, 0}, {2, 0}, {0, 0}, 257},
        {{-1, -8}, {2, -8}, {0, 1}, 257},
        {{-1, -8}, {2, -8}, {0, -1}, 0},
        {{8, 8}, {8, 8}, {-1, -1}, 257},
        {{8, 8}, {8, 8}, {1, 1}, 0},
        {{9, 0}, {9, 0}, {0, 0}, 0},
    };
    for (bool reverse : {false, true}) {
        if (reverse) { std::reverse(walk.begin(), walk.end()); }
        for (const auto& probe : probes) {
            EXPECT_EQ(next::internal::boundary_winding(
                          walk, probe.a, probe.b, probe.displacement),
                      reverse ? -probe.winding : probe.winding);
        }
    }
}

TEST(Clipper2NextTriangulationBoundaryTests, WindingUsesExactMidpointAtAdmittedExtremes) {
    constexpr int64_t limit = (std::numeric_limits<int64_t>::max)() >> 2;
    const next::Path64 square{{-limit, -limit}, {limit, -limit},
                              {limit, limit}, {-limit, limit}};
    EXPECT_EQ(next::internal::boundary_winding(square, {-limit, 0}, {limit, 1}, {0, 0}), 1);
    EXPECT_EQ(next::internal::boundary_winding(
                  square, {-limit, -limit}, {limit, -limit}, {0, 1}), 1);
    EXPECT_EQ(next::internal::boundary_winding(
                  square, {-limit, -limit}, {limit, -limit}, {0, -1}), 0);
    EXPECT_EQ(next::internal::boundary_winding(
                  square, {limit, limit}, {limit, limit}, {-1, -1}), 1);
    EXPECT_EQ(next::internal::boundary_winding(
                  square, {limit, limit}, {limit, limit}, {1, 1}), 0);
}

TEST(Clipper2NextTriangulationBoundaryTests, HoleFirstContactCancelsBeforeSectorValidation) {
    next::internal::triangulation_context context;
    const next::Paths64 paths{{{0, 5}, {2, 7}, {2, 3}},
                              {{0, 0}, {10, 0}, {10, 10}, {0, 10}}};
    ASSERT_EQ(next::internal::build_triangulation_boundary(context, paths),
              next::TriangulateResult::success);
    const auto touch = std::find_if(context.vertices.begin(), context.vertices.end(),
                                   [](auto vertex) { return vertex->pt == next::Point64(0, 5); });
    ASSERT_NE(touch, context.vertices.end());
    ASSERT_EQ((*touch)->event_end - (*touch)->event_begin, 2U);
    // The hole contributes -1 before the outer contributes +1 in its excluded sector.
    EXPECT_EQ(next::internal::boundary_winding(context.source_paths[0], {0, 5}, {0, 5}, {1, 0}), -1);
    EXPECT_EQ(next::internal::boundary_winding(context.source_paths[1], {0, 5}, {0, 5}, {1, 0}), 1);
    EXPECT_EQ(next::internal::event_for_ray(context, touch->get(), {1, 0}),
              next::internal::no_boundary_event);
    EXPECT_NE(next::internal::event_for_ray(context, touch->get(), {1, 2}),
              next::internal::no_boundary_event);
}

TEST(Clipper2NextTriangulationBoundaryTests, NestedRingsKeepAlternatingWinding) {
    next::internal::triangulation_context context;
    const next::Paths64 paths{{{2, 2}, {2, 8}, {8, 8}, {8, 2}},
                              {{4, 4}, {6, 4}, {6, 6}, {4, 6}},
                              {{0, 0}, {10, 0}, {10, 10}, {0, 10}}};
    ASSERT_EQ(next::internal::build_triangulation_boundary(context, paths),
              next::TriangulateResult::success);
    EXPECT_EQ(context.boundary_events.size(), 12U);
    const std::vector<std::pair<next::Point64, int64_t>> probes{
        {{1, 1}, 1}, {{3, 3}, 0}, {{5, 5}, 1}, {{11, 11}, 0}};
    for (const auto& [point, expected] : probes) {
        int64_t winding = 0;
        for (std::size_t ring = 0; ring != context.source_paths.size(); ++ring) {
            winding += next::internal::boundary_winding(context.source_paths[ring], point, point, {0, 0});
        }
        EXPECT_EQ(winding, expected);
    }
}

TEST(Clipper2NextTriangulationBoundaryTests, RejectsInvalidSameDirectionNestedRings) {
    next::internal::triangulation_context context;
    const next::Paths64 paths{{{0, 0}, {10, 0}, {10, 10}, {0, 10}},
                              {{2, 2}, {8, 2}, {8, 8}, {2, 8}}};
    EXPECT_THROW((void)next::internal::build_triangulation_boundary(context, paths),
                 next::clipper_error);
    EXPECT_TRUE(context.boundary_events.empty());
}
