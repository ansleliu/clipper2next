#include <clipper2next/geotypes/geotypes.hpp>
#include <clipper2next/core/point.h>
#include <clipper2next/core/rect.h>
#include <clipper2next/core/path_set.h>
#include <clipper2next/core/path_set_builder.h>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <ranges>
#include <type_traits>

namespace geo = geotypes;

static_assert(std::is_standard_layout_v<geo::Point2i64>);
static_assert(std::is_trivially_copyable_v<geo::Point2i64>);
static_assert(std::is_aggregate_v<geo::Point2i64>);
static_assert(sizeof(geo::Point2i64) == 16U);
static_assert(sizeof(geo::Rect2i64) == 32U);
static_assert(sizeof(geo::PathDescriptor) == 12U);
static_assert(sizeof(geo::RingDescriptor) == 12U);
static_assert(sizeof(geo::PolygonDescriptor) == 12U);
static_assert(std::same_as<clipper2next::Point64, geo::Point2i64>);
static_assert(std::same_as<clipper2next::PointD, geo::Point2d>);
static_assert(std::same_as<clipper2next::Rect64, geo::Rect2i64>);
static_assert(std::same_as<clipper2next::RectD, geo::Rect2d>);
static_assert(std::ranges::random_access_range<const clipper2next::path_set64>);
static_assert(!std::is_copy_constructible_v<clipper2next::path_set_builder64>);
static_assert(!std::is_move_constructible_v<clipper2next::path_set_builder64>);

TEST(Clipper2NextGeoTypesContractTests, FlatViewsBorrowExactPoolsWithoutOwningStorage) {
    const auto points = std::array{
        geo::Point2i64{0, 0},
        geo::Point2i64{8, 0},
        geo::Point2i64{8, 6},
        geo::Point2i64{0, 6},
    };
    const auto paths = std::array{
        geo::PathDescriptor{0U, 4U, geo::PathClosure::ClosedImplicit},
    };
    const auto rings = std::array{
        geo::RingDescriptor{0U, 4U, geo::RingRole::Shell},
    };
    const auto polygons = std::array{
        geo::PolygonDescriptor{0U, 1U, geo::noPolygonIndex},
    };

    const auto pathView = geo::PathSetView<std::int64_t>{points, paths};
    const auto ringSetView = geo::RingSetView<std::int64_t>{points, rings};
    const auto topologyView =
        geo::TopologyView<std::int64_t>{points, rings, polygons};

    EXPECT_EQ(pathView.points.data(), points.data());
    EXPECT_EQ(pathView.paths.data(), paths.data());
    EXPECT_EQ(ringSetView.points.data(), points.data());
    EXPECT_EQ(ringSetView.rings.data(), rings.data());
    EXPECT_EQ(ringSetView[0].data(), points.data());
    EXPECT_EQ(topologyView.points.data(), points.data());
    EXPECT_EQ(topologyView.rings.data(), rings.data());
    EXPECT_EQ(topologyView.polygons.data(), polygons.data());
}

TEST(Clipper2NextGeoTypesContractTests, OwningPathSetPublishesOneFlatPointPool) {
    clipper2next::path_set64 owner;
    const auto first = std::array{
        geo::Point2i64{0, 0}, geo::Point2i64{8, 0}, geo::Point2i64{8, 6}};
    const auto second = std::array{
        geo::Point2i64{20, 20}, geo::Point2i64{24, 20}};

    owner.reserve(2U, first.size() + second.size());
    owner.append(first, geo::PathClosure::ClosedImplicit);
    owner.append(second, geo::PathClosure::Open);

    const auto view = owner.view();
    ASSERT_EQ(view.points.size(), first.size() + second.size());
    ASSERT_EQ(view.paths.size(), 2U);
    EXPECT_EQ(view.points.data(), owner.points().data());
    EXPECT_EQ(view.paths.data(), owner.descriptors().data());
    EXPECT_EQ(owner[0].data(), owner.points().data());
    EXPECT_EQ(owner[1].data(), owner.points().data() + first.size());
    EXPECT_EQ(owner[0].closure, geo::PathClosure::ClosedImplicit);
    EXPECT_EQ(owner[1].closure, geo::PathClosure::Open);
}

TEST(Clipper2NextGeoTypesContractTests, UnfinishedFlatWriteTransactionRollsBackOwner) {
    auto owner = clipper2next::path_set64{};
    {
        auto builder = clipper2next::path_set_builder64{owner};
        builder.begin(1U, 4U);
        const auto destination =
            builder.acquire(4U, geo::PathClosure::ClosedImplicit);
        destination[0] = {0, 0};
    }

    EXPECT_TRUE(owner.empty());
    EXPECT_EQ(owner.point_count(), 0U);
}
