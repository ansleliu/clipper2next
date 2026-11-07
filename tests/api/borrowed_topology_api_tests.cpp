#include "clipper2next/clip.h"
#include "api/private/borrowed_topology_pipeline.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <future>
#include <functional>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace next = clipper2next;
namespace geo = geotypes;

namespace {

struct foreign_point final {
    std::int32_t x{};
    std::int32_t y{};
};

using foreign_path = std::vector<foreign_point>;
using foreign_paths = std::vector<foreign_path>;

template <typename Source>
concept can_borrow_paths64 = requires(Source&& source) {
    next::borrow_paths64(std::forward<Source>(source));
};

static_assert(can_borrow_paths64<foreign_paths&>);
static_assert(can_borrow_paths64<const foreign_paths&>);
static_assert(!can_borrow_paths64<foreign_paths>);

struct captured_ring final {
    std::size_t polygon_index{};
    next::topology_ring_role role{next::topology_ring_role::shell};
    std::vector<geo::Point2i64> points{};
};

struct collecting_topology_sink final {
    auto begin(const next::topology_layout64& value) -> next::clipper_error_code {
        ++begin_count;
        polygon_layouts.assign(value.polygons.begin(), value.polygons.end());
        ring_count = value.ring_count;
        point_count = value.point_count;
        rings.reserve(value.ring_count);
        return next::clipper_error_code::ok;
    }

    auto acquire(const next::topology_ring_layout64& value,
                 std::span<geo::Point2i64>& destination)
        -> next::clipper_error_code {
        if (write_result != next::clipper_error_code::ok) { return write_result; }
        rings.push_back(captured_ring{
            value.polygon_index,
            value.role,
            std::vector<geo::Point2i64>(value.point_count)});
        destination = rings.back().points;
        return next::clipper_error_code::ok;
    }

    auto finish() -> next::clipper_error_code {
        ++finish_count;
        return next::clipper_error_code::ok;
    }

    auto cancel() noexcept -> void { ++cancel_count; }

    std::vector<next::topology_polygon_layout64> polygon_layouts{};
    std::vector<captured_ring> rings{};
    std::size_t ring_count{};
    std::size_t point_count{};
    int begin_count{};
    int finish_count{};
    int cancel_count{};
    next::clipper_error_code write_result{next::clipper_error_code::ok};
};

struct throwing_topology_sink final {
    auto begin(const next::topology_layout64&) -> next::clipper_error_code {
        return next::clipper_error_code::ok;
    }

    auto acquire(const next::topology_ring_layout64&,
                 std::span<geo::Point2i64>&) -> next::clipper_error_code {
        throw std::runtime_error{"sink failed"};
    }

    auto finish() -> next::clipper_error_code { return next::clipper_error_code::ok; }
    auto cancel() noexcept -> void { ++cancel_count; }

    int cancel_count{};
};

struct reentrant_topology_sink final {
    auto begin(const next::topology_layout64&) -> next::clipper_error_code {
        return next::clipper_error_code::ok;
    }

    auto acquire(const next::topology_ring_layout64& ring,
                 std::span<geo::Point2i64>& destination)
        -> next::clipper_error_code {
        points.resize(ring.point_count);
        destination = points;
        if (!reentered) {
            reentered = true;
            auto nested_sink = collecting_topology_sink{};
            auto nested_request = next::borrowed_clip_request64{};
            nested_request.clip_type = next::ClipType::Union;
            nested_request.fill_rule = next::FillRule::EvenOdd;
            nested_request.subjects = next::borrow_paths64(*nested_subjects);
            const auto nested_result = next::clip_topology_checked(
                nested_request, next::make_topology_writer64(nested_sink));
            nested_succeeded =
                nested_result.has_value() && nested_sink.rings.size() == 1U;
            next::release_thread_caches();
        }
        return nested_succeeded ? next::clipper_error_code::ok
                                : next::clipper_error_code::sink_failure;
    }

    auto finish() -> next::clipper_error_code { return next::clipper_error_code::ok; }
    auto cancel() noexcept -> void { ++cancel_count; }

    const foreign_paths* nested_subjects{};
    std::vector<geo::Point2i64> points{};
    bool reentered{};
    bool nested_succeeded{};
    int cancel_count{};
};

struct changing_paths final {
    [[nodiscard]] auto begin() const noexcept { return values.begin(); }
    [[nodiscard]] auto end() const noexcept { return values.end(); }
    [[nodiscard]] auto size() const noexcept -> std::size_t {
        return size_call_count++ == 0 ? values.size() : 0U;
    }

    foreign_paths values{};
    mutable int size_call_count{};
};

struct inaccessible_path final {
    [[nodiscard]] auto begin() const noexcept { return values.begin(); }
    [[nodiscard]] auto end() const noexcept { return values.end(); }
    [[nodiscard]] auto size() const -> std::size_t {
        throw std::runtime_error{"path must not be inspected"};
    }

    foreign_path values{};
};

struct observed_paths final {
    [[nodiscard]] auto begin() const { ++path_access_count; return values.begin(); }
    [[nodiscard]] auto end() const { return values.end(); }
    [[nodiscard]] auto size() const -> std::size_t {
        ++size_call_count;
        if (on_size) { on_size(); }
        return values.size();
    }
    next::Paths64 values{};
    mutable int size_call_count{};
    mutable int path_access_count{};
    std::function<void()> on_size{};
};

[[nodiscard]] auto rectangle(std::int32_t left,
                             std::int32_t top,
                             std::int32_t right,
                             std::int32_t bottom) -> foreign_path {
    return {{left, top}, {right, top}, {right, bottom}, {left, bottom}};
}

[[nodiscard]] auto execute_union(const foreign_paths& subjects,
                                 collecting_topology_sink& sink)
    -> next::clipper_result<next::topology_write_stats64> {
    auto request = next::borrowed_clip_request64{};
    request.clip_type = next::ClipType::Union;
    request.fill_rule = next::FillRule::EvenOdd;
    request.subjects = next::borrow_paths64(subjects);
    return next::clip_topology_checked(request, next::make_topology_writer64(sink));
}

}  // namespace

TEST(Clipper2NextBorrowedTopologyApiTests, CleanupIncludesRecordsCreatedBySplitting) {
    const foreign_paths subjects{
        {{0, 0}, {4, 0}, {2, 0}, {4, 4}, {0, 4}},
        {{2, 1}, {6, 1}, {2, 5}, {2, 1}, {-2, 1}, {2, -3}},
    };
    for (const bool preserve : {false, true}) {
        for (const bool reverse : {false, true}) {
            auto request = next::borrowed_clip_request64{};
            request.clip_type = next::ClipType::Union;
            request.fill_rule = next::FillRule::EvenOdd;
            request.subjects = next::borrow_paths64(subjects);
            request.options.preserve_collinear = preserve;
            request.options.reverse_solution = reverse;
            auto sink = collecting_topology_sink{};
            const auto result = next::clip_topology_checked(
                request, next::make_topology_writer64(sink));
            ASSERT_TRUE(result.has_value());
            ASSERT_EQ(sink.rings.size(), 3U);
            EXPECT_EQ(result->output_polygon_count, 3U);
            EXPECT_EQ(result->output_ring_count, 3U);
            EXPECT_EQ(sink.polygon_layouts.size(), 3U);
            const auto triangle = std::find_if(sink.rings.begin(), sink.rings.end(), [](const auto& ring) {
                return ring.points.size() == 3U &&
                    std::find(ring.points.begin(), ring.points.end(), geo::Point2i64{6, 1}) != ring.points.end();
            });
            ASSERT_NE(triangle, sink.rings.end());
            EXPECT_NE(std::find(triangle->points.begin(), triangle->points.end(), geo::Point2i64{2, 1}), triangle->points.end());
            EXPECT_NE(std::find(triangle->points.begin(), triangle->points.end(), geo::Point2i64{3, 3}), triangle->points.end());
            EXPECT_EQ(triangle->role, next::topology_ring_role::shell);
            auto area2 = std::int64_t{};
            for (std::size_t i = 0; i < 3; ++i) {
                const auto first = triangle->points[i], second = triangle->points[(i + 1) % 3];
                area2 += first.x * second.y - first.y * second.x;
            }
            EXPECT_EQ(area2, reverse ? -8 : 8);

            request.limits.maximum_output_ring_count = 2;
            auto limited_sink = collecting_topology_sink{};
            const auto limited = next::clip_topology_checked(
                request, next::make_topology_writer64(limited_sink));
            ASSERT_FALSE(limited.has_value());
            EXPECT_EQ(limited.error(), next::clipper_error_code::resource_limit);
            EXPECT_EQ(limited_sink.begin_count, 0);
            EXPECT_EQ(limited_sink.cancel_count, 0);
        }
    }
}

TEST(Clipper2NextBorrowedTopologyApiTests, InvalidParametersPrecedeBindingAndEverySourceCallback) {
    for (const auto operation : {next::ClipType::Union, next::ClipType::NoClip}) {
        for (const auto empty : {false, true}) {
            for (const auto bound : {false, true}) {
                for (int invalid = 0; invalid != 3; ++invalid) {
                    SCOPED_TRACE(::testing::Message() << static_cast<int>(operation)
                                                     << '/' << empty << '/' << bound << '/' << invalid);
                    auto source = observed_paths{};
                    if (!empty) { source.values = {{{0, 0}, {10, 0}, {10, 10}, {0, 10}}}; }
                    auto sink = collecting_topology_sink{};
                    auto request = next::borrowed_clip_request64{};
                    request.clip_type = operation;
                    request.subjects = next::borrow_paths64(source);
                    if (invalid == 0) { request.clip_type = static_cast<next::ClipType>(99); }
                    if (invalid == 1) { request.fill_rule = static_cast<next::FillRule>(99); }
                    if (invalid == 2) {
                        request.options.intersection_policy.mode = static_cast<next::precision_mode>(99);
                    }
                    const auto result = next::clip_topology_checked(request,
                        bound ? next::make_topology_writer64(sink) : next::topology_writer64{});
                    ASSERT_FALSE(result);
                    EXPECT_EQ(result.error(), next::clipper_error_code::invalid_argument);
                    EXPECT_EQ(source.size_call_count, 0);
                    EXPECT_EQ(source.path_access_count, 0);
                    EXPECT_EQ(sink.begin_count, 0);
                    EXPECT_EQ(sink.cancel_count, 0);
                }
            }
        }
    }
    auto source = observed_paths{};
    auto request = next::borrowed_clip_request64{};
    request.subjects = next::borrow_paths64(source);
    const auto result = next::clip_topology_checked(request, next::topology_writer64{});
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), next::clipper_error_code::sink_failure);
    EXPECT_EQ(source.size_call_count, 0);
}

TEST(Clipper2NextBorrowedTopologyApiTests, NoClipValidatesRawGenericAndFlatCoordinatesBeforeNormalization) {
    for (const auto& path : {next::Path64{{next::MAX_COORD + 1, 0}},
                           next::Path64{{next::MAX_COORD + 1, 0}, {next::MAX_COORD + 1, 0}},
                           next::Path64{{0, 0}, {next::MAX_COORD + 1, 0}}}) {
        const auto paths = next::Paths64{path};
        auto points = std::vector<geo::Point2i64>{};
        for (const auto point : path) { points.push_back({point.x, point.y}); }
        const auto descriptors = std::array{geo::PathDescriptor{0U, static_cast<std::uint32_t>(points.size()), geo::PathClosure::ClosedImplicit}};
        for (const auto flat : {false, true}) {
            auto request = next::borrowed_clip_request64{};
            request.clip_type = next::ClipType::NoClip;
            request.subjects = flat ? next::borrow_paths64(geo::PathSetView64{points, descriptors})
                                    : next::borrow_paths64(paths);
            auto sink = collecting_topology_sink{};
            const auto result = next::clip_topology_checked(request, next::make_topology_writer64(sink));
            ASSERT_FALSE(result);
            EXPECT_EQ(result.error(), next::clipper_error_code::coordinate_range);
            EXPECT_EQ(sink.begin_count, 0);
        }
    }
}

TEST(Clipper2NextBorrowedTopologyApiTests, NoClipChargesOnlyMeasurementSlotsAndLeavesKernelUntouched) {
    const auto paths = next::Paths64{{{0, 0}, {100, 0}, {100, 100}, {0, 100}}};
    const auto points = std::array{geo::Point2i64{0, 0}, geo::Point2i64{100, 0},
                                  geo::Point2i64{100, 100}, geo::Point2i64{0, 100}};
    const auto descriptors = std::array{geo::PathDescriptor{0U, 4U, geo::PathClosure::ClosedImplicit}};
    for (const auto flat : {false, true}) {
        next::release_thread_caches();
        next::internal::clipper_base_state* cached_state{};
        {
            next::internal::borrowed_topology_engine_state_lease seed;
            cached_state = &seed.state();
            cached_state->cliptype_ = next::ClipType::Difference;
            cached_state->minima_list_sorted_ = true;
        }
        const auto original_minima_capacity = cached_state->minima_list_.capacity();
        const auto original_intersection_capacity = cached_state->intersect_nodes_.capacity();
        for (const auto warm : {false, true}) {
            SCOPED_TRACE(::testing::Message() << flat << '/' << warm);
            auto request = next::borrowed_clip_request64{};
            request.clip_type = next::ClipType::NoClip;
            request.subjects = flat ? next::borrow_paths64(geo::PathSetView64{points, descriptors})
                                    : next::borrow_paths64(paths);
            request.limits.maximum_engine_work = 0;
            request.limits.maximum_engine_workspace_bytes = 0;
            request.limits.maximum_staging_workspace_bytes =
                sizeof(next::path_source_contract::borrowed_path_measurement64);
            auto sink = collecting_topology_sink{};
            const auto result = next::clip_topology_checked(request, next::make_topology_writer64(sink));
            ASSERT_TRUE(result) << static_cast<int>(result.error());
            EXPECT_EQ(result->planned_engine_work, 0U);
            EXPECT_EQ(result->planned_engine_workspace_bytes, 0U);
            EXPECT_EQ(result->engine_input_point_writes, 0U);
            EXPECT_EQ(result->peak_workspace_bytes, request.limits.maximum_staging_workspace_bytes);
            EXPECT_EQ(sink.begin_count, 1);
            EXPECT_EQ(sink.finish_count, 1);
            EXPECT_EQ(sink.cancel_count, 0);
            EXPECT_TRUE(sink.rings.empty());
            EXPECT_EQ(cached_state->cliptype_, next::ClipType::Difference);
            EXPECT_TRUE(cached_state->minima_list_sorted_);
            EXPECT_EQ(cached_state->vertex_lists_.size(), 0U);
            EXPECT_EQ(cached_state->minima_list_.capacity(), original_minima_capacity);
            EXPECT_EQ(cached_state->intersect_nodes_.capacity(), original_intersection_capacity);
            --request.limits.maximum_staging_workspace_bytes;
            auto limited_sink = collecting_topology_sink{};
            const auto limited = next::clip_topology_checked(request, next::make_topology_writer64(limited_sink));
            ASSERT_FALSE(limited);
            EXPECT_EQ(limited.error(), next::clipper_error_code::resource_limit);
            EXPECT_EQ(limited_sink.begin_count, 0);
        }
    }
}

TEST(Clipper2NextBorrowedTopologyApiTests, InputPointAdmissionPrecedesCoordinateInspection) {
    struct unsigned_point { std::uint64_t x; std::uint64_t y; };
    const auto paths = std::vector<std::vector<unsigned_point>>{{{(std::numeric_limits<std::uint64_t>::max)(), 0}}};
    const auto points = std::array{geo::Point2i64{next::MAX_COORD + 1, 0}};
    const auto descriptors = std::array{geo::PathDescriptor{0U, 1U, geo::PathClosure::ClosedImplicit}};
    for (const auto flat : {false, true}) {
        auto request = next::borrowed_clip_request64{};
        request.subjects = flat ? next::borrow_paths64(geo::PathSetView64{points, descriptors})
                                : next::borrow_paths64(paths);
        request.limits.maximum_input_point_count = 0;
        auto sink = collecting_topology_sink{};
        const auto result = next::clip_topology_checked(request, next::make_topology_writer64(sink));
        ASSERT_FALSE(result);
        EXPECT_EQ(result.error(), next::clipper_error_code::resource_limit);
        EXPECT_EQ(sink.begin_count, 0);
    }
}

TEST(Clipper2NextBorrowedTopologyApiTests, MeasurementOnlyLeaseDoesNotResetRetainedEngine) {
    next::release_thread_caches();
    {
        next::internal::borrowed_topology_engine_state_lease seed;
        seed.state().cliptype_ = next::ClipType::Difference;
        seed.state().minima_list_sorted_ = true;
    }
    {
        next::internal::borrowed_topology_engine_state_lease measurement;
        EXPECT_EQ(measurement.state().cliptype_, next::ClipType::Difference);
        EXPECT_TRUE(measurement.state().minima_list_sorted_);
        auto& prepared = measurement.prepare_engine();
        EXPECT_EQ(&prepared, &measurement.state());
        EXPECT_EQ(prepared.cliptype_, next::ClipType::NoClip);
        EXPECT_FALSE(prepared.minima_list_sorted_);
    }
    next::release_thread_caches();
}

TEST(Clipper2NextBorrowedTopologyApiTests, EmptySourcesCompleteSinkProtocolWithoutKernelPreparation) {
    for (const auto operation : {next::ClipType::Union, next::ClipType::NoClip}) {
        for (const auto empty_path : {false, true}) {
            next::release_thread_caches();
            next::internal::clipper_base_state* cached_state{};
            {
                next::internal::borrowed_topology_engine_state_lease seed;
                cached_state = &seed.state();
                cached_state->cliptype_ = next::ClipType::Difference;
                cached_state->minima_list_sorted_ = true;
            }
            auto source = observed_paths{};
            if (empty_path) { source.values.emplace_back(); }
            auto request = next::borrowed_clip_request64{};
            request.clip_type = operation;
            request.subjects = next::borrow_paths64(source);
            request.limits.maximum_engine_work = 0;
            request.limits.maximum_engine_workspace_bytes = 0;
            auto sink = collecting_topology_sink{};
            const auto result = next::clip_topology_checked(request, next::make_topology_writer64(sink));
            ASSERT_TRUE(result);
            EXPECT_GT(source.size_call_count, 0);
            EXPECT_EQ(sink.begin_count, 1);
            EXPECT_EQ(sink.finish_count, 1);
            EXPECT_EQ(sink.cancel_count, 0);
            EXPECT_TRUE(sink.rings.empty());
            EXPECT_EQ(result->planned_engine_work, 0U);
            EXPECT_EQ(result->planned_engine_workspace_bytes, 0U);
            EXPECT_EQ(cached_state->cliptype_, next::ClipType::Difference);
            EXPECT_TRUE(cached_state->minima_list_sorted_);
        }
    }
}

TEST(Clipper2NextBorrowedTopologyApiTests, SourceCardinalityReentryAndReleaseAreProtectedByLease) {
    auto source = observed_paths{};
    source.values = {{{0, 0}, {100, 0}, {100, 100}, {0, 100}}};
    bool reentered = false;
    bool nested_succeeded = false;
    source.on_size = [&] {
        if (reentered) { return; }
        reentered = true;
        const auto nested_paths = next::Paths64{{{200, 0}, {300, 0}, {300, 100}, {200, 100}}};
        auto nested = next::borrowed_clip_request64{};
        nested.clip_type = next::ClipType::Union;
        nested.subjects = next::borrow_paths64(nested_paths);
        auto nested_sink = collecting_topology_sink{};
        nested_succeeded = next::clip_topology_checked(nested, next::make_topology_writer64(nested_sink)).has_value();
        next::release_thread_caches();
    };
    auto request = next::borrowed_clip_request64{};
    request.clip_type = next::ClipType::Union;
    request.subjects = next::borrow_paths64(source);
    auto sink = collecting_topology_sink{};
    const auto result = next::clip_topology_checked(request, next::make_topology_writer64(sink));
    ASSERT_TRUE(result);
    EXPECT_TRUE(reentered);
    EXPECT_TRUE(nested_succeeded);
    EXPECT_EQ(result->input_point_count, 4U);
    EXPECT_EQ(sink.rings.size(), 1U);
}

TEST(Clipper2NextBorrowedTopologyApiTests,
     ForeignPointRangesWriteDonutTopologyWithoutOwningPaths) {
    const auto subjects = foreign_paths{{
        {0, 0}, {100, 0}, {100, 100}, {0, 100},
    }};
    const auto clips = foreign_paths{{
        {25, 25}, {75, 25}, {75, 75}, {25, 75},
    }};
    auto sink_state = collecting_topology_sink{};

    auto request = next::borrowed_clip_request64{};
    request.clip_type = next::ClipType::Difference;
    request.fill_rule = next::FillRule::EvenOdd;
    request.subjects = next::borrow_paths64(subjects);
    request.clips = next::borrow_paths64(clips);

    const auto result = next::clip_topology_checked(
        request, next::make_topology_writer64(sink_state));

    ASSERT_TRUE(result.has_value())
        << static_cast<int>(result.error());
    ASSERT_EQ(sink_state.polygon_layouts.size(), 1U);
    EXPECT_EQ(sink_state.polygon_layouts[0].ring_count, 2U);
    EXPECT_EQ(sink_state.polygon_layouts[0].point_count, 8U);
    EXPECT_EQ(sink_state.ring_count, 2U);
    EXPECT_EQ(sink_state.point_count, 8U);
    ASSERT_EQ(sink_state.rings.size(), 2U);
    EXPECT_EQ(sink_state.rings[0].polygon_index, 0U);
    EXPECT_EQ(sink_state.rings[0].role, next::topology_ring_role::shell);
    EXPECT_EQ(sink_state.rings[1].polygon_index, 0U);
    EXPECT_EQ(sink_state.rings[1].role, next::topology_ring_role::hole);
    EXPECT_EQ(sink_state.begin_count, 1);
    EXPECT_EQ(sink_state.finish_count, 1);
    EXPECT_EQ(sink_state.cancel_count, 0);

    EXPECT_EQ(result->input_path_count, 2U);
    EXPECT_EQ(result->input_point_count, 8U);
    EXPECT_EQ(result->input_collection_point_writes, 0U);
    EXPECT_EQ(result->engine_input_point_writes, 8U);
    EXPECT_EQ(result->output_ring_acquire_count, 2U);
    EXPECT_EQ(result->output_final_point_writes, 8U);
    EXPECT_EQ(result->output_polygon_count, 1U);
    EXPECT_EQ(result->output_ring_count, 2U);
    EXPECT_EQ(result->output_point_count, 8U);
    EXPECT_GT(result->peak_workspace_bytes, 0U);
}

TEST(Clipper2NextBorrowedTopologyApiTests,
     GeoTypesFlatPathSetIsBorrowedWithoutAnOwningInputCollection) {
    const auto points = std::array{
        geo::Point2i64{0, 0},
        geo::Point2i64{100, 0},
        geo::Point2i64{100, 100},
        geo::Point2i64{0, 100},
    };
    const auto descriptors = std::array{
        geo::PathDescriptor{0U, 4U, geo::PathClosure::ClosedImplicit},
    };
    auto sink = collecting_topology_sink{};
    auto request = next::borrowed_clip_request64{};
    request.clip_type = next::ClipType::Union;
    request.subjects = next::borrow_paths64(
        geo::PathSetView64{points, descriptors});

    const auto result = next::clip_topology_checked(
        request, next::make_topology_writer64(sink));

    ASSERT_TRUE(result.has_value())
        << static_cast<int>(result.error());
    ASSERT_EQ(sink.rings.size(), 1U);
    ASSERT_EQ(sink.rings.front().points.size(), points.size());
    for (const auto expected : points) {
        EXPECT_NE(std::ranges::find(sink.rings.front().points, expected),
                  sink.rings.front().points.end());
    }
    EXPECT_EQ(result->input_collection_point_writes, 0U);
    EXPECT_EQ(result->engine_input_point_writes, points.size());
}

TEST(Clipper2NextBorrowedTopologyApiTests,
     GeoTypesTopologyViewBorrowsItsRingPoolDirectly) {
    const auto points = std::array{
        geo::Point2i64{0, 0},
        geo::Point2i64{100, 0},
        geo::Point2i64{100, 100},
        geo::Point2i64{0, 100},
    };
    const auto rings = std::array{
        geo::RingDescriptor{0U, 4U, geo::RingRole::Shell},
    };
    const auto polygons = std::array{
        geo::PolygonDescriptor{0U, 1U, geo::noPolygonIndex},
    };
    auto sink = collecting_topology_sink{};
    auto request = next::borrowed_clip_request64{};
    request.clip_type = next::ClipType::Union;
    request.subjects = next::borrow_paths64(
        geo::TopologyView64{points, rings, polygons});

    const auto result = next::clip_topology_checked(
        request, next::make_topology_writer64(sink));

    ASSERT_TRUE(result.has_value())
        << static_cast<int>(result.error());
    ASSERT_EQ(sink.rings.size(), 1U);
    EXPECT_EQ(result->input_collection_point_writes, 0U);
    EXPECT_EQ(result->engine_input_point_writes, points.size());
}

TEST(Clipper2NextBorrowedTopologyApiTests,
     ClosingAndConsecutiveDuplicatesAreNormalizedWithoutCollectionMaterialization) {
    const auto subjects = foreign_paths{{
        {0, 0}, {100, 0}, {100, 0}, {100, 100}, {0, 100}, {0, 0},
    }};
    auto sink = collecting_topology_sink{};

    const auto result = execute_union(subjects, sink);

    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(sink.rings.size(), 1U);
    EXPECT_EQ(sink.rings.front().points.size(), 4U);
    EXPECT_EQ(result->input_point_count, 6U);
    EXPECT_EQ(result->input_collection_point_writes, 0U);
    EXPECT_EQ(result->engine_input_point_writes, 5U);
    EXPECT_EQ(result->output_final_point_writes, 4U);
}

TEST(Clipper2NextBorrowedTopologyApiTests, NestedIslandRetainsPolygonParentAndRingRoles) {
    const auto subjects = foreign_paths{
        rectangle(0, 0, 100, 100),
        rectangle(20, 20, 80, 80),
        rectangle(40, 40, 60, 60),
    };
    auto sink = collecting_topology_sink{};

    const auto result = execute_union(subjects, sink);

    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(sink.polygon_layouts.size(), 2U);
    EXPECT_EQ(sink.polygon_layouts[0].parent_polygon_index,
              next::topology_no_polygon_index);
    EXPECT_EQ(sink.polygon_layouts[0].ring_count, 2U);
    EXPECT_EQ(sink.polygon_layouts[0].point_count, 8U);
    EXPECT_EQ(sink.polygon_layouts[1].parent_polygon_index, 0U);
    EXPECT_EQ(sink.polygon_layouts[1].ring_count, 1U);
    EXPECT_EQ(sink.polygon_layouts[1].point_count, 4U);
    ASSERT_EQ(sink.rings.size(), 3U);
    EXPECT_EQ(sink.rings[0].role, next::topology_ring_role::shell);
    EXPECT_EQ(sink.rings[1].role, next::topology_ring_role::hole);
    EXPECT_EQ(sink.rings[2].role, next::topology_ring_role::shell);
    EXPECT_EQ(sink.rings[2].polygon_index, 1U);
}

TEST(Clipper2NextBorrowedTopologyApiTests,
     NonZeroXorPreservesIslandInsideTouchingComplexHole) {
    const auto subjects = foreign_paths{
        {{15385, 15385}, {23077, 15385}, {23077, 23077}, {15385, 23077}},
        {{0, 0},
         {61538, 0},
         {61538, 7692},
         {38462, 7692},
         {38462, 15385},
         {53846, 15385},
         {53846, 23077},
         {38462, 23077},
         {38462, 30769},
         {61538, 30769},
         {61538, 38462},
         {0, 38462}},
        {{7692, 7692}, {7692, 30769}, {30769, 30769}, {30769, 7692}},
        {{69231, 15385}, {76923, 7692}, {92308, 23077}, {84615, 30769}},
    };
    const auto clips = foreign_paths{
        {{7692, 7692}, {30769, 7692}, {30769, 30769}, {7692, 30769}},
        {{15385, 15385}, {15385, 23077}, {23077, 23077}, {23077, 15385}},
        {{61538, 0},
         {100000, 0},
         {100000, 38462},
         {61538, 38462},
         {61538, 30769},
         {38462, 30769},
         {38462, 23077},
         {61538, 23077},
         {61538, 15385},
         {38462, 15385},
         {38462, 7692},
         {61538, 7692}},
        {{69231, 7692},
         {69231, 30769},
         {92308, 30769},
         {92308, 7692}},
    };
    auto sink = collecting_topology_sink{};
    auto request = next::borrowed_clip_request64{};
    request.clip_type = next::ClipType::Xor;
    request.fill_rule = next::FillRule::NonZero;
    request.subjects = next::borrow_paths64(subjects);
    request.clips = next::borrow_paths64(clips);

    const auto result = next::clip_topology_checked(
        request, next::make_topology_writer64(sink));

    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(sink.polygon_layouts.size(), 2U);
    EXPECT_EQ(sink.polygon_layouts[0].ring_count, 3U);
    EXPECT_EQ(sink.polygon_layouts[1].ring_count, 1U);
    EXPECT_EQ(sink.polygon_layouts[1].parent_polygon_index, 0U);
    ASSERT_EQ(sink.rings.size(), 4U);
    EXPECT_EQ(sink.rings[0].role, next::topology_ring_role::shell);
    EXPECT_EQ(sink.rings[1].role, next::topology_ring_role::hole);
    EXPECT_EQ(sink.rings[2].role, next::topology_ring_role::hole);
    EXPECT_EQ(sink.rings[3].role, next::topology_ring_role::shell);
}

TEST(Clipper2NextBorrowedTopologyApiTests, TopologyMatchesOwningClipTreePointForPoint) {
    const auto subjects = foreign_paths{rectangle(0, 0, 100, 100)};
    const auto clips = foreign_paths{rectangle(25, 25, 75, 75)};
    auto sink = collecting_topology_sink{};
    auto borrowed_request = next::borrowed_clip_request64{};
    borrowed_request.clip_type = next::ClipType::Difference;
    borrowed_request.fill_rule = next::FillRule::EvenOdd;
    borrowed_request.subjects = next::borrow_paths64(subjects);
    borrowed_request.clips = next::borrow_paths64(clips);

    const auto borrowed_result = next::clip_topology_checked(
        borrowed_request, next::make_topology_writer64(sink));

    auto owning_request = next::clip_request64{};
    owning_request.clip_type = next::ClipType::Difference;
    owning_request.fill_rule = next::FillRule::EvenOdd;
    owning_request.subjects = next::Paths64{next::Path64{
        {0, 0}, {100, 0}, {100, 100}, {0, 100}}};
    owning_request.clips = next::Paths64{next::Path64{
        {25, 25}, {75, 25}, {75, 75}, {25, 75}}};
    const auto owning_result = next::clip_tree_checked(owning_request);

    ASSERT_TRUE(borrowed_result.has_value());
    ASSERT_TRUE(owning_result.has_value());
    ASSERT_EQ(sink.rings.size(), 2U);
    ASSERT_EQ(owning_result->tree.count(), 1U);
    const auto shell = owning_result->tree.child(owning_result->tree.root(), 0U);
    ASSERT_EQ(owning_result->tree.count(shell), 1U);
    const auto hole = owning_result->tree.child(shell, 0U);
    const auto sameCoordinates = [](const auto& first, const auto& second) {
        return first.x == second.x && first.y == second.y;
    };
    EXPECT_TRUE(std::ranges::equal(
        sink.rings[0].points, owning_result->tree.polygon(shell), sameCoordinates));
    EXPECT_TRUE(std::ranges::equal(
        sink.rings[1].points, owning_result->tree.polygon(hole), sameCoordinates));
}

TEST(Clipper2NextBorrowedTopologyApiTests, InputMutationBetweenPassesIsRejected) {
    const auto source = changing_paths{{rectangle(0, 0, 100, 100)}};
    auto sink = collecting_topology_sink{};
    auto request = next::borrowed_clip_request64{};
    request.clip_type = next::ClipType::Union;
    request.subjects = next::borrow_paths64(source);

    const auto result = next::clip_topology_checked(
        request, next::make_topology_writer64(sink));

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), next::clipper_error_code::input_changed);
    EXPECT_EQ(sink.begin_count, 0);
}

TEST(Clipper2NextBorrowedTopologyApiTests, CoordinateAndResourceLimitsFailBeforeSinkBegin) {
    const auto out_of_range = foreign_paths{{
        {0, 0},
        {100, 0},
        {(std::numeric_limits<std::int32_t>::max)(), 100},
        {0, 100},
    }};
    auto coordinate_sink = collecting_topology_sink{};
    auto coordinate_request = next::borrowed_clip_request64{};
    coordinate_request.clip_type = next::ClipType::Union;
    coordinate_request.subjects = next::borrow_paths64(out_of_range);

    // int32 coordinates are all inside Clipper's guarded int64 lattice.
    ASSERT_TRUE(next::clip_topology_checked(
                    coordinate_request, next::make_topology_writer64(coordinate_sink))
                    .has_value());

    const auto subjects = foreign_paths{rectangle(0, 0, 100, 100)};
    auto limited_sink = collecting_topology_sink{};
    auto limited_request = next::borrowed_clip_request64{};
    limited_request.clip_type = next::ClipType::Union;
    limited_request.subjects = next::borrow_paths64(subjects);
    limited_request.limits.maximum_input_point_count = 3U;
    const auto input_limited = next::clip_topology_checked(
        limited_request, next::make_topology_writer64(limited_sink));
    ASSERT_FALSE(input_limited.has_value());
    EXPECT_EQ(input_limited.error(), next::clipper_error_code::resource_limit);
    EXPECT_EQ(limited_sink.begin_count, 0);

    limited_request.limits.maximum_input_point_count = 4U;
    limited_request.limits.maximum_output_point_count = 3U;
    const auto output_limited = next::clip_topology_checked(
        limited_request, next::make_topology_writer64(limited_sink));
    ASSERT_FALSE(output_limited.has_value());
    EXPECT_EQ(output_limited.error(), next::clipper_error_code::resource_limit);
    EXPECT_EQ(limited_sink.begin_count, 0);

    limited_request.limits = {};
    limited_request.limits.maximum_engine_work = 0U;
    const auto work_limited = next::clip_topology_checked(
        limited_request, next::make_topology_writer64(limited_sink));
    ASSERT_FALSE(work_limited.has_value());
    EXPECT_EQ(work_limited.error(), next::clipper_error_code::resource_limit);
    EXPECT_EQ(limited_sink.begin_count, 0);

    limited_request.limits = {};
    limited_request.limits.maximum_engine_workspace_bytes = 0U;
    const auto engine_workspace_limited = next::clip_topology_checked(
        limited_request, next::make_topology_writer64(limited_sink));
    ASSERT_FALSE(engine_workspace_limited.has_value());
    EXPECT_EQ(
        engine_workspace_limited.error(),
        next::clipper_error_code::resource_limit);
    EXPECT_EQ(limited_sink.begin_count, 0);
}

TEST(Clipper2NextBorrowedTopologyApiTests, InputPathLimitIsCheckedBeforePathInspection) {
    const auto subjects = std::vector<inaccessible_path>{{rectangle(0, 0, 100, 100)}};
    auto sink = collecting_topology_sink{};
    auto request = next::borrowed_clip_request64{};
    request.clip_type = next::ClipType::Union;
    request.subjects = next::borrow_paths64(subjects);
    request.limits.maximum_input_path_count = 0U;

    const auto result = next::clip_topology_checked(
        request, next::make_topology_writer64(sink));

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), next::clipper_error_code::resource_limit);
    EXPECT_EQ(sink.begin_count, 0);

    request.limits.maximum_input_path_count =
        (std::numeric_limits<std::size_t>::max)();
    request.limits.maximum_staging_workspace_bytes = 0U;
    const auto workspace_limited = next::clip_topology_checked(
        request, next::make_topology_writer64(sink));
    ASSERT_FALSE(workspace_limited.has_value());
    EXPECT_EQ(workspace_limited.error(), next::clipper_error_code::resource_limit);
    EXPECT_EQ(sink.begin_count, 0);

}

TEST(Clipper2NextBorrowedTopologyApiTests,
     StagingWorkspaceLimitIsIndependentOfRetainedThreadCacheCapacity) {
    next::release_thread_caches();
    const auto small_subjects = foreign_paths{rectangle(0, 0, 100, 100)};
    auto baseline_sink = collecting_topology_sink{};
    const auto baseline = execute_union(small_subjects, baseline_sink);
    ASSERT_TRUE(baseline.has_value());

    auto large_path = foreign_path{};
    large_path.reserve(4096U);
    for (std::int32_t x = 0; x < 2048; ++x) { large_path.push_back({x, 0}); }
    for (std::int32_t x = 2047; x >= 0; --x) { large_path.push_back({x, 100}); }
    const auto large_subjects = foreign_paths{std::move(large_path)};
    auto warm_sink = collecting_topology_sink{};
    const auto warm = execute_union(large_subjects, warm_sink);
    ASSERT_TRUE(warm.has_value());
    ASSERT_GT(warm->output_point_count, baseline->output_point_count);

    auto limited_sink = collecting_topology_sink{};
    auto limited_request = next::borrowed_clip_request64{};
    limited_request.clip_type = next::ClipType::Union;
    limited_request.subjects = next::borrow_paths64(small_subjects);
    limited_request.limits.maximum_staging_workspace_bytes = baseline->peak_workspace_bytes;
    const auto limited = next::clip_topology_checked(
        limited_request, next::make_topology_writer64(limited_sink));

    ASSERT_TRUE(limited.has_value());
    EXPECT_EQ(limited->peak_workspace_bytes, baseline->peak_workspace_bytes);
    EXPECT_EQ(limited_sink.finish_count, 1);
}

TEST(Clipper2NextBorrowedTopologyApiTests,
     StagingWorkspaceIsReusedUntilThreadCachesAreReleased) {
    const auto subjects = foreign_paths{rectangle(0, 0, 100, 100)};
    next::release_thread_caches();

    auto cold_sink = collecting_topology_sink{};
    const auto cold = execute_union(subjects, cold_sink);
    ASSERT_TRUE(cold.has_value());
    EXPECT_GT(cold->staging_reallocation_count, 0U);

    auto warm_sink = collecting_topology_sink{};
    const auto warm = execute_union(subjects, warm_sink);
    ASSERT_TRUE(warm.has_value());
    EXPECT_EQ(warm->staging_reallocation_count, 0U);

    next::release_thread_caches();
    auto released_sink = collecting_topology_sink{};
    const auto released = execute_union(subjects, released_sink);
    ASSERT_TRUE(released.has_value());
    EXPECT_GT(released->staging_reallocation_count, 0U);
}

TEST(Clipper2NextBorrowedTopologyApiTests, Int64CoordinateOutsideGuardedLatticeIsRejected) {
    using wide_point = struct wide_point_definition final {
        std::int64_t x{};
        std::int64_t y{};
    };
    using wide_paths = std::vector<std::vector<wide_point>>;
    const auto subjects = wide_paths{{
        {0, 0},
        {(std::numeric_limits<std::int64_t>::max)(), 0},
        {100, 100},
        {0, 100},
    }};
    auto sink = collecting_topology_sink{};
    auto request = next::borrowed_clip_request64{};
    request.clip_type = next::ClipType::Union;
    request.subjects = next::borrow_paths64(subjects);

    const auto result = next::clip_topology_checked(
        request, next::make_topology_writer64(sink));

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), next::clipper_error_code::coordinate_range);
    EXPECT_EQ(sink.begin_count, 0);
}

TEST(Clipper2NextBorrowedTopologyApiTests, SinkFailureCancelsTransaction) {
    const auto subjects = foreign_paths{rectangle(0, 0, 100, 100)};
    auto rejecting_sink = collecting_topology_sink{};
    rejecting_sink.write_result = next::clipper_error_code::sink_failure;

    const auto rejected = execute_union(subjects, rejecting_sink);

    ASSERT_FALSE(rejected.has_value());
    EXPECT_EQ(rejected.error(), next::clipper_error_code::sink_failure);
    EXPECT_EQ(rejecting_sink.begin_count, 1);
    EXPECT_EQ(rejecting_sink.finish_count, 0);
    EXPECT_EQ(rejecting_sink.cancel_count, 1);

    auto throwing_sink = throwing_topology_sink{};
    auto request = next::borrowed_clip_request64{};
    request.clip_type = next::ClipType::Union;
    request.subjects = next::borrow_paths64(subjects);
    const auto thrown = next::clip_topology_checked(
        request, next::make_topology_writer64(throwing_sink));
    ASSERT_FALSE(thrown.has_value());
    EXPECT_EQ(thrown.error(), next::clipper_error_code::sink_failure);
    EXPECT_EQ(throwing_sink.cancel_count, 1);

    auto retry_sink = collecting_topology_sink{};
    const auto retried = execute_union(subjects, retry_sink);
    ASSERT_TRUE(retried);
    ASSERT_EQ(retry_sink.rings.size(), 1U);
    EXPECT_EQ(retry_sink.rings.front().points.size(), 4U);
    EXPECT_EQ(retry_sink.finish_count, 1);
}

TEST(Clipper2NextBorrowedTopologyApiTests,
     SinkCanReenterBorrowedTopologyAndReleaseThreadCaches) {
    const auto outer_subjects = foreign_paths{rectangle(0, 0, 100, 100)};
    const auto nested_subjects = foreign_paths{rectangle(200, 200, 240, 240)};
    auto sink = reentrant_topology_sink{.nested_subjects = &nested_subjects};
    auto request = next::borrowed_clip_request64{};
    request.clip_type = next::ClipType::Union;
    request.subjects = next::borrow_paths64(outer_subjects);

    const auto result = next::clip_topology_checked(
        request, next::make_topology_writer64(sink));

    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(sink.reentered);
    EXPECT_TRUE(sink.nested_succeeded);
    EXPECT_EQ(sink.cancel_count, 0);

    auto after_release_sink = collecting_topology_sink{};
    const auto after_release = execute_union(outer_subjects, after_release_sink);
    ASSERT_TRUE(after_release.has_value());
    EXPECT_GT(after_release->staging_reallocation_count, 0U);
}

TEST(Clipper2NextBorrowedTopologyApiTests, SharedImmutableBorrowedInputIsConcurrentAndDeterministic) {
    const auto subjects = foreign_paths{
        rectangle(0, 0, 100, 100),
        rectangle(20, 20, 80, 80),
        rectangle(40, 40, 60, 60),
    };
    std::vector<std::future<std::vector<captured_ring>>> executions;
    executions.reserve(8U);
    for (int worker = 0; worker < 8; ++worker) {
        executions.emplace_back(std::async(std::launch::async, [&subjects] {
            auto sink = collecting_topology_sink{};
            const auto result = execute_union(subjects, sink);
            if (!result.has_value()) { return std::vector<captured_ring>{}; }
            return sink.rings;
        }));
    }

    const auto reference = executions.front().get();
    ASSERT_EQ(reference.size(), 3U);
    for (std::size_t index = 1U; index < executions.size(); ++index) {
        const auto result = executions[index].get();
        ASSERT_EQ(result.size(), reference.size());
        for (std::size_t ring = 0U; ring < reference.size(); ++ring) {
            EXPECT_EQ(result[ring].polygon_index, reference[ring].polygon_index);
            EXPECT_EQ(result[ring].role, reference[ring].role);
            EXPECT_EQ(result[ring].points, reference[ring].points);
        }
    }
}

TEST(Clipper2NextBorrowedTopologyApiTests,
     IntegerSweepPreservesLegacyTouchCycleRings) {
    const auto subjects = foreign_paths{
        {{0, 0}, {100000, 0}, {100000, 100000}, {0, 100000}},
        {{25000, 25000}, {25000, 50000}, {50000, 25000}},
        {{25000, 50000}, {25000, 75000}, {50000, 75000}},
        {{50000, 75000}, {75000, 75000}, {75000, 50000}},
    };
    const auto clips = foreign_paths{
        {{50000, 25000}, {75000, 25000}, {75000, 50000}},
    };
    auto request = next::borrowed_clip_request64{};
    request.clip_type = next::ClipType::Difference;
    request.fill_rule = next::FillRule::NonZero;
    request.subjects = next::borrow_paths64(subjects);
    request.clips = next::borrow_paths64(clips);
    auto sink = collecting_topology_sink{};

    const auto result = next::clip_topology_checked(
        request, next::make_topology_writer64(sink));

    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(sink.polygon_layouts.size(), 1U);
    EXPECT_EQ(sink.polygon_layouts[0].ring_count, 5U);
    EXPECT_EQ(sink.ring_count, 5U);
}

#include "api/private/checked_execution.h"
TEST(Clipper2NextBorrowedTopologyApiTests, NoClipLedgerMeasuresDescriptorsNotBorrowedPointStorage) {
    for (const auto count : {4U, 4000U}) {
        next::Paths64 source(1);
        for (std::size_t i = 0; i < count; ++i) { source[0].push_back({static_cast<std::int64_t>(i), 0}); }
        auto request = next::borrowed_clip_request64{};
        request.clip_type = next::ClipType::NoClip;
        request.subjects = next::borrow_paths64(source);
        constexpr auto bytes = sizeof(next::path_source_contract::borrowed_path_measurement64);
        for (const auto limit : {std::size_t{0}, bytes - 1, bytes}) {
            auto context = next::internal::execution_resource_ledger{};
            context.maximum_work = 0;
            context.maximum_workspace_bytes = 0;
            context.maximum_staging_bytes = limit;
            auto sink = collecting_topology_sink{};
            auto writer = next::make_topology_writer64(sink);
            const auto result = next::internal::checked_call<next::topology_write_stats64>([&] {
                return next::internal::execute_borrowed_topology(request, writer, &context);
            });
            EXPECT_EQ(result.has_value(), limit == bytes);
            EXPECT_EQ(context.engine_high_water, 0U);
            EXPECT_EQ(context.work, 0U);
            EXPECT_EQ(context.engine_bytes, 0U);
            EXPECT_EQ(context.staging_bytes, 0U);
            EXPECT_EQ(sink.begin_count, limit == bytes ? 1 : 0);
            if (result) {
                EXPECT_EQ(result->planned_engine_workspace_bytes, 0U);
                EXPECT_EQ(result->planned_engine_work, 0U);
                EXPECT_EQ(context.staging_high_water, bytes);
            }
        }
    }
}

TEST(Clipper2NextBorrowedTopologyApiTests, DescriptorAdmissionPrecedesSinkBeginAndSurvivesRootDestruction) {
    const next::Paths64 source{{{0, 0}, {100, 0}, {100, 100}, {0, 100}}};
    auto request = next::borrowed_clip_request64{};
    request.clip_type = next::ClipType::Union;
    request.subjects = next::borrow_paths64(source);
    auto baseline = collecting_topology_sink{};
    const auto reference = next::clip_topology_checked(request, next::make_topology_writer64(baseline));
    ASSERT_TRUE(reference);
    for (auto limit : {reference->peak_workspace_bytes - 1U, reference->peak_workspace_bytes}) {
        auto context = next::internal::execution_resource_ledger{};
        context.maximum_staging_bytes = limit;
        auto sink = collecting_topology_sink{};
        auto writer = next::make_topology_writer64(sink);
        const auto result = next::internal::checked_call<next::topology_write_stats64>([&] {
            return next::internal::execute_borrowed_topology(request, writer, &context);
        });
        EXPECT_EQ(result.has_value(), limit == reference->peak_workspace_bytes);
        EXPECT_EQ(sink.begin_count, result ? 1 : 0);
        EXPECT_GT(context.engine_high_water, 0U);
        EXPECT_LE(context.staging_high_water, limit);
        EXPECT_EQ(context.engine_bytes, 0U);
        EXPECT_EQ(context.staging_bytes, 0U);
    }
    // All operation roots are gone. Reuse and release must not dereference them.
    next::clip_request64 owning;
    owning.clip_type = next::ClipType::Union;
    owning.subjects = source;
    ASSERT_TRUE(next::clip_checked(owning));
    auto sink = collecting_topology_sink{};
    ASSERT_TRUE(next::clip_topology_checked(request, next::make_topology_writer64(sink)));
    next::release_thread_caches();
}

TEST(Clipper2NextBorrowedTopologyApiTests, EveryRetainedOwnerDetachesBeforeUnbudgetedLeaseReuse) {
    std::printf("T09 sizeof Vertex=%zu OutRec=%zu OutPt=%zu Minima=%zu Horz=%zu token=%zu\n",
        sizeof(next::internal::Vertex), sizeof(next::internal::output_record_node),
        sizeof(next::internal::output_point_node), sizeof(next::internal::local_minimum_node),
        sizeof(next::internal::horizontal_segment_node), sizeof(next::internal::scoped_workspace_charge));
    const next::Paths64 source{{{0, 0}, {100, 0}, {100, 100}, {0, 100}}};
    next::borrowed_clip_request64 request;
    request.clip_type = next::ClipType::Union;
    request.subjects = next::borrow_paths64(source);
    auto root = next::internal::execution_resource_ledger{};
    auto sink = collecting_topology_sink{};
    auto writer = next::make_topology_writer64(sink);
    ASSERT_TRUE(next::internal::execute_borrowed_topology(request, writer, &root));
    // A lingering zero-byte binding would still reject these actual unbudgeted
    // growth operations. Merely checking the previous totals cannot prove this.
    root.maximum_workspace_bytes = 0;
    root.maximum_staging_bytes = 0;
    {
        next::internal::borrowed_topology_engine_state_lease lease;
        auto& state = lease.prepare_engine();
        next::internal::Vertex vertex{{0, 0}};
        ASSERT_NO_THROW(static_cast<void>(state.vertex_lists_.acquire(8)));
        ASSERT_NO_THROW(static_cast<void>(state.active_pool_.emplace()));
        ASSERT_NO_THROW(state.minima_list_.emplace_back(vertex, next::PathType::Subject, false));
        ASSERT_NO_THROW(next::internal::push_scanline(state.scanline_list_, 100));
        ASSERT_NO_THROW(state.intersect_nodes_.emplace_back());
        ASSERT_NO_THROW(state.scanbeam_top_edges_.push_back(nullptr));
        ASSERT_NO_THROW(state.scanbeam_unit_edges_.push_back(nullptr));
        ASSERT_NO_THROW(state.scanbeam_unit_scratch_.push_back(nullptr));
        ASSERT_NO_THROW(state.horz_seg_list_.emplace_back());
        ASSERT_NO_THROW(state.horz_join_list_.emplace_back());
        ASSERT_NO_THROW({
            auto& record = state.output_owner_.create_outrec();
            static_cast<void>(state.output_owner_.create_outpt({0, 0}, record));
            record.splits.emplace_back(record);
        });
        auto& workspace = lease.workspace();
        ASSERT_NO_THROW(workspace.subjects.paths.resize(1));
        ASSERT_NO_THROW(workspace.clips.paths.resize(1));
        ASSERT_NO_THROW(workspace.polygon_layouts.resize(1));
        ASSERT_NO_THROW(workspace.closed_layout.resize(1));
        ASSERT_NO_THROW(workspace.record_metadata.resize(1));
        ASSERT_NO_THROW(workspace.next_ring_by_polygon.resize(1));
    }
    EXPECT_EQ(root.engine_bytes, 0U);
    EXPECT_EQ(root.staging_bytes, 0U);
}

namespace {
struct checking_reused_ring_sink final {
    const std::vector<captured_ring>* expected;
    std::array<geo::Point2i64, 4> buffer{};
    std::size_t next_ring{};
    std::size_t current_size{};
    int begin_count{};
    auto previous_matches() const -> bool {
        return next_ring == 0U || std::ranges::equal(
            std::span{buffer}.first(current_size), (*expected)[next_ring - 1U].points);
    }
    auto begin(const next::topology_layout64& layout) -> next::clipper_error_code {
        ++begin_count;
        return layout.ring_count == expected->size() && layout.maximum_ring_point_count <= buffer.size()
            ? next::clipper_error_code::ok : next::clipper_error_code::sink_failure;
    }
    auto acquire(const next::topology_ring_layout64& ring, std::span<geo::Point2i64>& destination)
        -> next::clipper_error_code {
        if (!previous_matches() || next_ring >= expected->size()) { return next::clipper_error_code::sink_failure; }
        const auto& reference = (*expected)[next_ring];
        if (ring.point_count != reference.points.size() || ring.polygon_index != reference.polygon_index ||
            ring.role != reference.role) { return next::clipper_error_code::sink_failure; }
        current_size = ring.point_count;
        destination = std::span{buffer}.first(current_size);
        ++next_ring;
        return next::clipper_error_code::ok;
    }
    auto finish() -> next::clipper_error_code {
        return next_ring == expected->size() && previous_matches()
            ? next::clipper_error_code::ok : next::clipper_error_code::sink_failure;
    }
    auto cancel() noexcept -> void { current_size = 0U; }
};
}
TEST(Clipper2NextBorrowedTopologyApiTests, ExternalSinkStorageStrategyDoesNotChangeLibraryOwnedBudget) {
    const foreign_paths source{rectangle(0, 0, 10, 10), rectangle(30, 0, 40, 10), rectangle(60, 0, 70, 10)};
    next::borrowed_clip_request64 request;
    request.clip_type = next::ClipType::Union;
    request.subjects = next::borrow_paths64(source);
    auto reference = collecting_topology_sink{};
    const auto baseline = next::clip_topology_checked(request, next::make_topology_writer64(reference));
    ASSERT_TRUE(baseline);
    ASSERT_EQ(reference.rings.size(), 3U);
    ASSERT_EQ(baseline->output_point_count, 12U);
    request.limits.maximum_staging_workspace_bytes = baseline->peak_workspace_bytes;
    request.limits.maximum_output_point_count = 12;
    request.limits.maximum_output_ring_count = 3;
    auto collected = collecting_topology_sink{};
    auto streamed = checking_reused_ring_sink{&reference.rings};
    const auto retained = next::clip_topology_checked(request, next::make_topology_writer64(collected));
    const auto reused = next::clip_topology_checked(request, next::make_topology_writer64(streamed));
    ASSERT_TRUE(retained);
    ASSERT_TRUE(reused);
    EXPECT_EQ(retained->peak_workspace_bytes, reused->peak_workspace_bytes);
    EXPECT_EQ(retained->planned_engine_workspace_bytes, reused->planned_engine_workspace_bytes);
    EXPECT_EQ(retained->output_final_point_writes, reused->output_final_point_writes);
    EXPECT_EQ(reused->output_final_point_writes, 12U);
    EXPECT_EQ(streamed.buffer.size(), 4U); // one ring is reused; collector retains all three
    for (std::size_t i = 0; i < reference.rings.size(); ++i) { EXPECT_EQ(collected.rings[i].points, reference.rings[i].points); }
    request.limits.maximum_output_point_count = 11;
    auto rejected_collector = collecting_topology_sink{};
    auto rejected_stream = checking_reused_ring_sink{&reference.rings};
    const auto first = next::clip_topology_checked(request, next::make_topology_writer64(rejected_collector));
    const auto second = next::clip_topology_checked(request, next::make_topology_writer64(rejected_stream));
    ASSERT_FALSE(first);
    ASSERT_FALSE(second);
    EXPECT_EQ(first.error(), next::clipper_error_code::resource_limit);
    EXPECT_EQ(second.error(), next::clipper_error_code::resource_limit);
    EXPECT_EQ(rejected_collector.begin_count, 0);
    EXPECT_EQ(rejected_stream.begin_count, 0);
}

TEST(Clipper2NextBorrowedTopologyApiTests, FlatCleanupPreservesEveryRingAndChargesItsFinalOwner) {
    const std::array<next::Paths64, 2> inputs{
        next::Paths64{{{0, 0}, {40, 0}, {40, 40}, {0, 40}},
            {{5, 5}, {5, 35}, {35, 35}, {35, 5}},
            {{10, 10}, {30, 10}, {30, 30}, {10, 30}},
            {{15, 15}, {15, 25}, {25, 25}, {25, 15}}},
        next::Paths64{{{0, 0}, {4, 0}, {2, 0}, {4, 4}, {0, 4}},
            {{2, 1}, {6, 1}, {2, 5}, {2, 1}, {-2, 1}, {2, -3}}},
    };
    for (const auto& input : inputs) {
        for (const bool reverse : {false, true}) {
            auto request = next::borrowed_clip_request64{};
            request.subjects = next::borrow_paths64(input);
            request.clip_type = next::ClipType::Union;
            request.fill_rule = next::FillRule::EvenOdd;
            request.options.reverse_solution = reverse;
            auto expected = next::clip_request64{};
            expected.subjects = input;
            expected.clip_type = request.clip_type;
            expected.fill_rule = request.fill_rule;
            expected.options = request.options;
            const auto reference = next::clip(expected).closed;
            next::internal::execution_resource_ledger resources;
            next::internal::scoped_workspace_charge charge{&resources, next::internal::workspace_category::staging};
            next::path_set64 result;
            ASSERT_EQ(next::internal::execute_borrowed_closed_paths(request, result, charge), next::clipper_error_code::ok);
            ASSERT_EQ(result.size(), reference.size());
            std::vector<bool> used(reference.size());
            for (const auto path : result) {
                auto matched = false;
                for (std::size_t ring = 0; ring < reference.size() && !matched; ++ring) {
                    if (used[ring] || reference[ring].size() != path.size()) { continue; }
                    for (std::size_t start = 0; start < path.size() && !matched; ++start) {
                        auto equal = true;
                        for (std::size_t i = 0; i < path.size(); ++i) {
                            equal = equal && path[i] == reference[ring][(start + i) % path.size()];
                        }
                        if (equal) { used[ring] = true; matched = true; }
                    }
                }
                EXPECT_TRUE(matched);
            }
            EXPECT_EQ(charge.bytes(), result.point_count() * sizeof(next::Point64) + result.size() * sizeof(geo::PathDescriptor));
            EXPECT_EQ(resources.staging_bytes, charge.bytes());
            EXPECT_EQ(resources.engine_bytes, 0U);
            EXPECT_EQ(resources.reserved_engine_bytes, 0U);
            EXPECT_EQ(resources.reserved_staging_bytes, 0U);
        }
    }
}

TEST(Clipper2NextBorrowedTopologyApiTests, FlatNoClipMeasuresInputWithoutCreatingKernelOwners) {
    const next::Paths64 input{{{0, 0}, {10, 0}, {10, 10}, {0, 10}}};
    auto request = next::borrowed_clip_request64{};
    request.subjects = next::borrow_paths64(input);
    request.clip_type = next::ClipType::NoClip;
    request.limits.maximum_engine_work = 0;
    request.limits.maximum_engine_workspace_bytes = 0;
    next::internal::execution_resource_ledger resources;
    resources.maximum_work = 0;
    resources.maximum_workspace_bytes = 0;
    resources.maximum_staging_bytes = sizeof(next::path_source_contract::borrowed_path_measurement64);
    next::internal::scoped_workspace_charge charge{&resources, next::internal::workspace_category::staging};
    next::path_set64 output;
    EXPECT_EQ(next::internal::execute_borrowed_closed_paths(request, output, charge), next::clipper_error_code::ok);
    EXPECT_TRUE(output.empty());
    EXPECT_EQ(resources.work, 0U);
    EXPECT_EQ(resources.engine_high_water, 0U);
    EXPECT_EQ(resources.staging_high_water, sizeof(next::path_source_contract::borrowed_path_measurement64));
    EXPECT_EQ(resources.staging_bytes, 0U);
}
