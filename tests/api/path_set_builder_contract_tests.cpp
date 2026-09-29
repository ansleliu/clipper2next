#include <clipper2next/core/path_set_builder.h>
#include <clipper2next/clip/topology.h>

#include <gtest/gtest.h>

#include <array>
#include <limits>
#include <stdexcept>

namespace next = clipper2next;
namespace geo = geotypes;

TEST(Clipper2NextPathSetBuilderContractTests, InactiveDestructionLeavesExistingOwnerUntouched) {
    auto owner = next::path_set64{};
    const auto points = std::array{geo::Point2i64{3, 4}};
    owner.append(points, geo::PathClosure::Open);
    { const auto builder = next::path_set_builder64{owner}; }
    ASSERT_EQ(owner.point_count(), 1U);
    EXPECT_EQ(owner[0][0], points[0]);
}

TEST(Clipper2NextPathSetBuilderContractTests, FailedAcquireAndFinishRetainCandidateUntilCancel) {
    auto owner = next::path_set64{};
    auto builder = next::path_set_builder64{owner};
    builder.begin(2, 2);
    const auto first = builder.acquire(1, geo::PathClosure::Open);
    first[0] = {3, 4};
    EXPECT_THROW(builder.finish(), std::logic_error);
    EXPECT_THROW(static_cast<void>(builder.acquire(2, geo::PathClosure::Open)), std::logic_error);
    EXPECT_EQ(owner.size(), 1U);
    EXPECT_EQ(owner.point_count(), 2U);
    EXPECT_EQ(owner[0][0], (geo::Point2i64{3, 4}));
    const auto storage = owner.points().data();
    builder.cancel();
    EXPECT_TRUE(owner.empty());
    EXPECT_EQ(owner.point_count(), 0U);
    builder.begin(2, 2);
    EXPECT_EQ(owner.points().data(), storage);
}

TEST(Clipper2NextPathSetBuilderContractTests, StableDisjointSpansSurviveAcquireAndSuccessfulDestruction) {
    auto owner = next::path_set64{};
    {
        auto builder = next::path_set_builder64{owner};
        builder.begin(2, 3);
        const auto first = builder.acquire(1, geo::PathClosure::Open);
        const auto second = builder.acquire(2, geo::PathClosure::Open);
        EXPECT_EQ(first.data() + first.size(), second.data());
        first[0] = {1, 2};
        second[0] = {3, 4};
        second[1] = {5, 6};
        builder.finish();
    }
    ASSERT_EQ(owner.size(), 2U);
    EXPECT_EQ(owner[0][0], (geo::Point2i64{1, 2}));
    EXPECT_EQ(owner[1][1], (geo::Point2i64{5, 6}));
}

TEST(Clipper2NextPathSetBuilderContractTests, BeginRangeFailureClearsPreviousContents) {
    auto owner = next::path_set64{};
    const auto points = std::array{geo::Point2i64{3, 4}};
    owner.append(points, geo::PathClosure::Open);
    auto builder = next::path_set_builder64{owner};
    const auto tooMany = static_cast<std::size_t>((std::numeric_limits<std::uint32_t>::max)()) + 1U;
    EXPECT_THROW(builder.begin(1, tooMany), std::length_error);
    EXPECT_TRUE(owner.empty());
    EXPECT_EQ(owner.point_count(), 0U);
}

namespace {

struct failing_writer {
    next::path_set64 owner;
    next::path_set_builder64 builder{owner};
    int failure;
    int cancels{};

    explicit failing_writer(int stage) : failure{stage} {}
    auto begin(const next::topology_layout64& layout) -> next::clipper_error_code {
        builder.begin(layout.ring_count, layout.point_count);
        if (failure == 0) { return next::clipper_error_code::sink_failure; }
        return next::clipper_error_code::ok;
    }
    auto acquire(const next::topology_ring_layout64& ring, std::span<geo::Point2i64>& destination)
        -> next::clipper_error_code {
        destination = builder.acquire(ring.point_count, geo::PathClosure::ClosedImplicit);
        if (failure == 1) { throw std::runtime_error{"writer failure"}; }
        if (failure == 2) { destination = destination.first(destination.size() - 1); }
        return next::clipper_error_code::ok;
    }
    auto finish() -> next::clipper_error_code {
        builder.finish();
        return next::clipper_error_code::sink_failure;
    }
    void cancel() noexcept { ++cancels; builder.cancel(); }
};

}  // namespace

TEST(Clipper2NextPathSetBuilderContractTests, WriterFailureCancelsAtEveryPublicationBoundary) {
    const next::Paths64 paths{{{0, 0}, {10, 0}, {10, 10}, {0, 10}}};
    auto request = next::borrowed_clip_request64{};
    request.clip_type = next::ClipType::Union;
    request.subjects = next::borrow_paths64(paths);
    for (int stage = 0; stage < 4; ++stage) {
        auto writer = failing_writer{stage};
        const auto result = next::clip_topology_checked(request, next::make_topology_writer64(writer));
        EXPECT_FALSE(result);
        EXPECT_EQ(writer.cancels, 1);
        EXPECT_TRUE(writer.owner.empty());
        EXPECT_EQ(writer.owner.point_count(), 0U);
    }
}
