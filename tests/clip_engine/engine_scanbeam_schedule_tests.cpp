#include "clip/engine/private/engine_active_list.h"
#include "clip/engine/private/engine_geometry.h"
#include "clip/engine/private/engine_intersections.h"
#include "clip/engine/private/engine_scanbeam_schedule.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <tuple>
#include <vector>

namespace next = clipper2next;

namespace {

auto configure_edge(next::internal::active_edge_node& edge,
                    int64_t bottom_x,
                    int64_t top_x,
                    int64_t bottom_y = 10,
                    int64_t top_y = 0) -> void {
    edge.bottom = {bottom_x, bottom_y};
    edge.top_point = {top_x, top_y};
    edge.current_x = bottom_x;
    edge.dx = next::internal::get_dx(edge.bottom, edge.top_point);
}

template <std::size_t Size>
auto link_ael(std::array<next::internal::active_edge_node, Size>& edges) -> void {
    for (std::size_t index = 0; index < edges.size(); ++index) {
        edges[index].prev_in_ael = index == 0U ? nullptr : &edges[index - 1U];
        edges[index].next_in_ael =
            index + 1U == edges.size() ? nullptr : &edges[index + 1U];
    }
}

template <std::size_t Size>
auto wire_unit_run_sel(std::array<next::internal::active_edge_node, Size>& edges)
    -> next::internal::active_edge_node_ref {
    for (std::size_t index = 0; index < edges.size(); ++index) {
        edges[index].current_x = next::internal::top_x(edges[index], 0);
        edges[index].prev_in_sel = index == 0U ? nullptr : &edges[index - 1U];
        edges[index].next_in_sel =
            index + 1U == edges.size() ? nullptr : &edges[index + 1U];
        edges[index].jump = edges[index].next_in_sel;
    }
    return &edges.front();
}

using intersection_signature = std::tuple<int, int, int64_t, int64_t>;

template <std::size_t Size>
auto capture_intersections(const std::array<next::internal::active_edge_node, Size>& edges,
                           const next::internal::IntersectNodeList& intersections)
    -> std::vector<intersection_signature> {
    static_assert(Size > 0U);
    const auto edge_index = [&edges](const next::internal::active_edge_node& edge) {
        for (std::size_t index = 0; index < edges.size(); ++index) {
            if (&edges[index] == &edge) { return static_cast<int>(index); }
        }
        throw std::logic_error{"intersection references an edge outside the test fixture"};
    };

    std::vector<intersection_signature> signatures;
    signatures.reserve(intersections.size());
    for (const next::internal::IntersectNode& intersection : intersections) {
        const int first = edge_index(intersection.first_edge());
        const int second = edge_index(intersection.second_edge());
        signatures.emplace_back(
            std::min(first, second),
            std::max(first, second),
            intersection.pt.x,
            intersection.pt.y);
    }
    std::sort(signatures.begin(), signatures.end());
    return signatures;
}

template <std::size_t Size>
auto capture_intersections_in_order(
    const std::array<next::internal::active_edge_node, Size>& edges,
    const next::internal::IntersectNodeList& intersections)
    -> std::vector<intersection_signature> {
    const auto edge_index = [&edges](const next::internal::active_edge_node& edge) {
        for (std::size_t index = 0; index < edges.size(); ++index) {
            if (&edges[index] == &edge) { return static_cast<int>(index); }
        }
        throw std::logic_error{"intersection references an edge outside the test fixture"};
    };

    std::vector<intersection_signature> signatures;
    signatures.reserve(intersections.size());
    for (const next::internal::IntersectNode& intersection : intersections) {
        signatures.emplace_back(edge_index(intersection.first_edge()),
                                edge_index(intersection.second_edge()),
                                intersection.pt.x,
                                intersection.pt.y);
    }
    return signatures;
}

}  // namespace

TEST(Clipper2NextEngineScanbeamScheduleTests, SortedActiveListBuildsOneGlobalRun) {
    std::array<next::internal::active_edge_node, 4U> edges;
    configure_edge(edges[0], 0, 0);
    configure_edge(edges[1], 10, 10);
    configure_edge(edges[2], 20, 20);
    configure_edge(edges[3], 30, 30);
    link_ael(edges);
    next::internal::active_edge_node_ref sel;
    std::vector<next::internal::active_edge_node*> top_edges;

    const auto schedule = next::internal::prepare_global_scanbeam_schedule(
        edges.front(), 0, sel, top_edges);

    EXPECT_FALSE(schedule.has_inversion);
    EXPECT_EQ(schedule.active_edge_count, edges.size());
    EXPECT_EQ(schedule.inversion_count, 0U);
    EXPECT_EQ(sel.get(), nullptr);
    EXPECT_EQ(edges[0].jump.get(), nullptr);
    EXPECT_EQ(edges[1].jump.get(), nullptr);
    EXPECT_EQ(edges[2].jump.get(), nullptr);
    EXPECT_EQ(edges[3].jump.get(), nullptr);
    ASSERT_EQ(top_edges.size(), edges.size());
    EXPECT_EQ(top_edges.front(), &edges.front());
    EXPECT_EQ(top_edges.back(), &edges.back());
}

TEST(Clipper2NextEngineScanbeamScheduleTests, InvertedActiveListBuildsMaximalRunJumps) {
    std::array<next::internal::active_edge_node, 4U> edges;
    configure_edge(edges[0], 0, 0);
    configure_edge(edges[1], 10, 30);
    configure_edge(edges[2], 20, 20);
    configure_edge(edges[3], 30, 40);
    link_ael(edges);
    next::internal::active_edge_node_ref sel;
    std::vector<next::internal::active_edge_node*> top_edges;

    const auto schedule = next::internal::prepare_global_scanbeam_schedule(
        edges.front(),
        0,
        sel,
        top_edges,
        next::internal::scanbeam_schedule_mode::monotone_runs);

    EXPECT_TRUE(schedule.has_inversion);
    EXPECT_EQ(schedule.active_edge_count, edges.size());
    EXPECT_EQ(schedule.inversion_count, 1U);
    EXPECT_EQ(sel.get(), &edges[0]);
    EXPECT_EQ(edges[0].jump.get(), &edges[2]);
    EXPECT_EQ(edges[1].jump.get(), nullptr);
    EXPECT_EQ(edges[2].jump.get(), nullptr);
    EXPECT_EQ(edges[3].jump.get(), nullptr);
    EXPECT_EQ(edges[1].next_in_sel.get(), &edges[2]);
    EXPECT_EQ(edges[2].prev_in_sel.get(), &edges[1]);
}

TEST(Clipper2NextEngineScanbeamScheduleTests, InvertedActiveListDefaultsToUnitRunJumps) {
    std::array<next::internal::active_edge_node, 4U> edges;
    configure_edge(edges[0], 0, 0);
    configure_edge(edges[1], 10, 30);
    configure_edge(edges[2], 20, 20);
    configure_edge(edges[3], 30, 40);
    link_ael(edges);
    next::internal::active_edge_node_ref sel;
    std::vector<next::internal::active_edge_node*> top_edges;

    const auto schedule = next::internal::prepare_global_scanbeam_schedule(
        edges.front(), 0, sel, top_edges);

    EXPECT_TRUE(schedule.has_inversion);
    EXPECT_EQ(sel.get(), &edges[0]);
    EXPECT_EQ(edges[0].jump.get(), &edges[1]);
    EXPECT_EQ(edges[1].jump.get(), &edges[2]);
    EXPECT_EQ(edges[2].jump.get(), &edges[3]);
    EXPECT_EQ(edges[3].jump.get(), nullptr);
}

TEST(Clipper2NextEngineScanbeamScheduleTests,
     ContiguousUnitRunsMatchLegacyLinkedGenerationOrderExhaustively) {
    std::array<int, 6U> top_order{0, 1, 2, 3, 4, 5};
    do {
        std::array<next::internal::active_edge_node, 6U> linked_edges;
        std::array<next::internal::active_edge_node, 6U> contiguous_edges;
        for (std::size_t index = 0; index < top_order.size(); ++index) {
            configure_edge(linked_edges[index],
                           static_cast<int64_t>(index * 10U),
                           static_cast<int64_t>(top_order[index] * 10));
            configure_edge(contiguous_edges[index],
                           static_cast<int64_t>(index * 10U),
                           static_cast<int64_t>(top_order[index] * 10));
        }

        auto linked_sel = wire_unit_run_sel(linked_edges);
        next::internal::IntersectNodeList linked_intersections;
        const auto linked_has_intersections =
            next::internal::build_intersection_list_from_sel(
                linked_sel, linked_intersections, 0, 10);

        std::vector<next::internal::active_edge_node*> contiguous_order;
        contiguous_order.reserve(contiguous_edges.size());
        for (auto& edge : contiguous_edges) {
            edge.current_x = next::internal::top_x(edge, 0);
            contiguous_order.push_back(&edge);
        }
        std::vector<next::internal::active_edge_node*> scratch;
        next::internal::IntersectNodeList contiguous_intersections;
        const auto contiguous_has_intersections =
            next::internal::build_intersection_list_from_contiguous_unit_runs(
                contiguous_order, scratch, contiguous_intersections, 0, 10);

        ASSERT_EQ(contiguous_has_intersections, linked_has_intersections);
        EXPECT_EQ(capture_intersections_in_order(contiguous_edges, contiguous_intersections),
                  capture_intersections_in_order(linked_edges, linked_intersections));

        next::internal::sort_intersections(linked_intersections);
        next::internal::sort_intersections(contiguous_intersections);
        EXPECT_EQ(capture_intersections_in_order(contiguous_edges, contiguous_intersections),
                  capture_intersections_in_order(linked_edges, linked_intersections));
    } while (std::next_permutation(top_order.begin(), top_order.end()));
}

TEST(Clipper2NextEngineScanbeamScheduleTests, TopEdgesMirrorGlobalSelOrder) {
    std::array<next::internal::active_edge_node, 4U> edges;
    configure_edge(edges[0], 0, 0);
    configure_edge(edges[1], 10, 30);
    configure_edge(edges[2], 20, 20);
    configure_edge(edges[3], 30, 40, 10, 5);
    link_ael(edges);
    next::internal::active_edge_node_ref sel;
    std::vector<next::internal::active_edge_node*> top_edges;

    const auto schedule = next::internal::prepare_global_scanbeam_schedule(
        edges.front(), 0, sel, top_edges);

    ASSERT_TRUE(schedule.has_inversion);
    ASSERT_EQ(top_edges.size(), 3U);
    EXPECT_EQ(top_edges[0], &edges[0]);
    EXPECT_EQ(top_edges[1], &edges[1]);
    EXPECT_EQ(top_edges.back(), &edges[2]);
    EXPECT_EQ(sel.get(), &edges[0]);
    EXPECT_EQ(edges[0].next_in_sel.get(), &edges[1]);
}

TEST(Clipper2NextEngineScanbeamScheduleTests,
     OffsetCleanupMonotoneScheduleMatchesUnitRunForSingleBoundaryFixture) {
    std::array<next::internal::active_edge_node, 4U> run_edges;
    configure_edge(run_edges[0], 0, 30);
    configure_edge(run_edges[1], 10, 0);
    configure_edge(run_edges[2], 20, 40);
    configure_edge(run_edges[3], 30, 10);
    link_ael(run_edges);
    next::internal::active_edge_node_ref run_sel;
    std::vector<next::internal::active_edge_node*> top_edges;
    const auto schedule = next::internal::prepare_global_scanbeam_schedule(
        run_edges.front(),
        0,
        run_sel,
        top_edges,
        next::internal::scanbeam_schedule_mode::monotone_runs);
    ASSERT_TRUE(schedule.has_inversion);
    next::internal::IntersectNodeList run_intersections;

    const bool run_has_intersections = next::internal::build_intersection_list_from_sel(
        run_sel, run_intersections, 0, 10);

    std::array<next::internal::active_edge_node, 4U> unit_edges;
    configure_edge(unit_edges[0], 0, 30);
    configure_edge(unit_edges[1], 10, 0);
    configure_edge(unit_edges[2], 20, 40);
    configure_edge(unit_edges[3], 30, 10);
    auto unit_sel = wire_unit_run_sel(unit_edges);
    next::internal::IntersectNodeList unit_intersections;

    const bool unit_has_intersections = next::internal::build_intersection_list_from_sel(
        unit_sel, unit_intersections, 0, 10);

    EXPECT_EQ(run_has_intersections, unit_has_intersections);
    EXPECT_EQ(capture_intersections(run_edges, run_intersections),
              capture_intersections(unit_edges, unit_intersections));
}

TEST(Clipper2NextEngineScanbeamScheduleTests,
     OffsetCleanupMonotoneScheduleMatchesUnitRunForMultiBoundaryFixture) {
    std::array<next::internal::active_edge_node, 6U> run_edges;
    configure_edge(run_edges[0], 0, 50);
    configure_edge(run_edges[1], 10, 10);
    configure_edge(run_edges[2], 20, 40);
    configure_edge(run_edges[3], 30, 20);
    configure_edge(run_edges[4], 40, 60);
    configure_edge(run_edges[5], 50, 0);
    link_ael(run_edges);
    next::internal::active_edge_node_ref run_sel;
    std::vector<next::internal::active_edge_node*> top_edges;
    const auto schedule = next::internal::prepare_global_scanbeam_schedule(
        run_edges.front(),
        0,
        run_sel,
        top_edges,
        next::internal::scanbeam_schedule_mode::monotone_runs);
    ASSERT_TRUE(schedule.has_inversion);
    EXPECT_EQ(schedule.active_edge_count, run_edges.size());
    EXPECT_EQ(schedule.inversion_count, 3U);
    next::internal::IntersectNodeList run_intersections;

    const bool run_has_intersections = next::internal::build_intersection_list_from_sel(
        run_sel, run_intersections, 0, 10);

    std::array<next::internal::active_edge_node, 6U> unit_edges;
    configure_edge(unit_edges[0], 0, 50);
    configure_edge(unit_edges[1], 10, 10);
    configure_edge(unit_edges[2], 20, 40);
    configure_edge(unit_edges[3], 30, 20);
    configure_edge(unit_edges[4], 40, 60);
    configure_edge(unit_edges[5], 50, 0);
    auto unit_sel = wire_unit_run_sel(unit_edges);
    next::internal::IntersectNodeList unit_intersections;

    const bool unit_has_intersections = next::internal::build_intersection_list_from_sel(
        unit_sel, unit_intersections, 0, 10);

    EXPECT_EQ(run_has_intersections, unit_has_intersections);
    EXPECT_EQ(capture_intersections(run_edges, run_intersections),
              capture_intersections(unit_edges, unit_intersections));
}
