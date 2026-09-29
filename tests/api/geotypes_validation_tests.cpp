#include <clipper2next/geotypes/validation.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <limits>
#include <vector>

namespace geo = geotypes;

namespace {
void expectStructureError(const std::expected<void, geo::StructureError>& result,
                          geo::StructureError error) {
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), error);
}
}  // namespace

TEST(Clipper2NextGeoTypesValidationTests, EmptyViewsAndSharedPoolSubsetsAreStructurallyValid) {
    EXPECT_TRUE(geo::validateStructure(geo::PathView64{}));
    EXPECT_TRUE(geo::validateStructure(geo::PathSetView64{}));
    EXPECT_TRUE(geo::validateStructure(geo::RingSetView64{}));
    EXPECT_TRUE(geo::validateStructure(geo::TopologyView64{}, std::span<std::uint8_t>{}));
    const auto points = std::array<geo::Point2i64, 12>{};
    const auto paths = std::array{geo::PathDescriptor{4, 3, geo::PathClosure::Open},
                                   geo::PathDescriptor{4, 3, geo::PathClosure::ClosedRepeated}};
    EXPECT_TRUE(geo::validateStructure(geo::PathSetView64{points, paths}));
    const auto rings = std::array{geo::RingDescriptor{4, 3, geo::RingRole::Shell, {}}};
    EXPECT_TRUE(geo::validateStructure(geo::RingSetView64{points, rings}));
}

TEST(Clipper2NextGeoTypesValidationTests, RejectsPathEnumsAndRangesWithoutOverflowingArithmetic) {
    const auto points = std::array<geo::Point2i64, 4>{};
    auto path = geo::PathDescriptor{0, 4, geo::PathClosure::Open};
    const auto view = geo::PathSetView64{points, std::span{&path, 1U}};
    path.pointOffset = 5;
    expectStructureError(geo::validateStructure(view), geo::StructureError::PointRange);
    path.pointOffset = (std::numeric_limits<std::uint32_t>::max)();
    path.pointCount = 2;
    expectStructureError(geo::validateStructure(view), geo::StructureError::PointRange);
    path.pointOffset = 3;
    path.pointCount = 2;
    expectStructureError(geo::validateStructure(view), geo::StructureError::PointRange);
    path.pointOffset = 4;
    path.pointCount = 0;
    EXPECT_TRUE(geo::validateStructure(view));
    path.closure = static_cast<geo::PathClosure>(255);
    expectStructureError(geo::validateStructure(view), geo::StructureError::InvalidClosure);
    expectStructureError(geo::validateStructure(geo::PathView64{points, path.closure}), geo::StructureError::InvalidClosure);
}

TEST(Clipper2NextGeoTypesValidationTests, RingChecksDoNotConsumePolygonParents) {
    const auto points = std::array<geo::Point2i64, 4>{};
    auto ring = geo::RingDescriptor{0, 4, geo::RingRole::Shell, {}};
    auto polygon = geo::PolygonDescriptor{0, 1, 99};
    const auto topology = geo::TopologyView64{points, std::span{&ring, 1U}, std::span{&polygon, 1U}};
    auto scratch = std::array<std::uint8_t, 1>{};
    EXPECT_TRUE(geo::validateStructure(topology.ringSet()));
    expectStructureError(geo::validateStructure(topology, scratch), geo::StructureError::ParentRange);
    polygon.parentPolygon = geo::noPolygonIndex;
    polygon.ringOffset = 1;
    expectStructureError(geo::validateStructure(topology, scratch), geo::StructureError::RingRange);
    polygon.ringOffset = 0;
    ring.role = static_cast<geo::RingRole>(255);
    expectStructureError(geo::validateStructure(topology.ringSet()), geo::StructureError::InvalidRole);
    ring.role = geo::RingRole::Shell;
    ring.pointCount = 5;
    expectStructureError(geo::validateStructure(topology.ringSet()), geo::StructureError::PointRange);
}

TEST(Clipper2NextGeoTypesValidationTests, ParentGraphUsesCallerScratchAndRejectsSelfAndMultiNodeCycles) {
    auto polygons = std::array{geo::PolygonDescriptor{0, 0, 1},
                                 geo::PolygonDescriptor{0, 0, geo::noPolygonIndex},
                                 geo::PolygonDescriptor{0, 0, 1}};
    const auto topology = geo::TopologyView64{{}, {}, polygons};
    auto scratch = std::array<std::uint8_t, 4>{255, 255, 255, 123};
    expectStructureError(geo::validateStructure(topology, std::span{scratch}.first(2)), geo::StructureError::ScratchTooSmall);
    EXPECT_TRUE(geo::validateStructure(topology, scratch));
    EXPECT_EQ(scratch.back(), 123);
    polygons[1].parentPolygon = 1;
    expectStructureError(geo::validateStructure(topology, scratch), geo::StructureError::ParentCycle);
    polygons[1].parentPolygon = 2;
    polygons[2].parentPolygon = 0;
    expectStructureError(geo::validateStructure(topology, scratch), geo::StructureError::ParentCycle);
}

TEST(Clipper2NextGeoTypesValidationTests, DeepForwardChainDoesNotRecurseOrRescanEveryAncestor) {
    constexpr auto count = std::uint32_t{100000};
    auto polygons = std::vector<geo::PolygonDescriptor>(count);
    auto scratch = std::vector<std::uint8_t>(count);
    for (auto index = std::uint32_t{}; index + 1 < count; ++index) {
        polygons[index].parentPolygon = index + 1;
    }
    const auto topology = geo::TopologyView64{{}, {}, polygons};
    EXPECT_TRUE(geo::validateStructure(topology, scratch));
    polygons.back().parentPolygon = count / 2;
    expectStructureError(geo::validateStructure(topology, scratch), geo::StructureError::ParentCycle);
}
