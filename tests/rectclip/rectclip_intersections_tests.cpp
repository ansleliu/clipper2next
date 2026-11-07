#include <gtest/gtest.h>

#include "rectclip/private/rectclip_intersections.h"

#include <concepts>
#include <span>

namespace next = clipper2next;

namespace {

template <class RectArg>
concept rect_intersection_input = requires(RectArg rect,
                                           next::Point64 first,
                                           next::Point64 second,
                                           next::internal::rect_location location,
                                           next::Point64 intersection) {
    {
        next::internal::get_intersection(rect, first, second, location, intersection)
    } -> std::same_as<bool>;
};

static_assert(rect_intersection_input<const next::Rect64&>);
static_assert(!rect_intersection_input<std::span<const next::Point64>>);

}  // namespace

TEST(Clipper2NextRectClipIntersectionsTests, SegmentIntersectionFindsCrossingPoint) {
    next::Point64 intersection;

    const auto found =
        next::internal::get_segment_intersection({0, 0}, {10, 10}, {0, 10}, {10, 0}, intersection);

    EXPECT_TRUE(found);
    EXPECT_EQ(intersection, next::Point64(5, 5));
}

TEST(Clipper2NextRectClipIntersectionsTests, SegmentIntersectionFindsEndpointTouch) {
    next::Point64 intersection;

    const auto found =
        next::internal::get_segment_intersection({0, 0}, {10, 0}, {10, 0}, {10, 10}, intersection);

    EXPECT_TRUE(found);
    EXPECT_EQ(intersection, next::Point64(10, 0));
}

TEST(Clipper2NextRectClipIntersectionsTests, SegmentIntersectionRejectsCollinearOverlap) {
    next::Point64 intersection{77, 88};

    const auto found =
        next::internal::get_segment_intersection({0, 0}, {10, 0}, {5, 0}, {15, 0}, intersection);

    EXPECT_FALSE(found);
}

TEST(Clipper2NextRectClipIntersectionsTests, AxisBoundaryKeepsEndpointAndDirectionSemantics) {
    // The same crossing in both segment directions must select the same point.
    // Include translated coordinate limits; only comparisons are needed to
    // classify a point against a horizontal or vertical supporting line.
    for (const auto origin : {next::MIN_COORD + 32, int64_t{0}, next::MAX_COORD - 32}) {
        for (const bool vertical : {false, true}) {
            const auto point = [&](int64_t along, int64_t across) {
                return vertical ? next::Point64{origin + across, origin + along}
                                : next::Point64{origin + along, origin + across};
            };
            for (const bool reverse : {false, true}) {
                const auto edge_start = point(reverse ? 10 : 0, 0);
                const auto edge_end = point(reverse ? 0 : 10, 0);
                for (const int64_t along : {-1, 0, 5, 10, 11}) {
                    // At the coordinate limits test exact endpoint touches;
                    // keep the established floating interpolation unchanged.
                    const auto first = point(along, origin == 0 ? -5 : 0);
                    const auto last = point(along, 5);
                    for (const bool reverse_input : {false, true}) {
                        next::Point64 intersection{77, 88};
                        const auto found = next::internal::get_segment_intersection(
                            reverse_input ? last : first, reverse_input ? first : last,
                            edge_start, edge_end, intersection);
                        EXPECT_EQ(found, along >= 0 && along <= 10);
                        if (found) { EXPECT_EQ(intersection, point(along, 0)); }
                    }
                }
                next::Point64 intersection;
                EXPECT_FALSE(next::internal::get_segment_intersection(
                    point(2, 0), point(8, 0), edge_start, edge_end, intersection));
            }
        }
    }
}

TEST(Clipper2NextRectClipIntersectionsTests, RectIntersectionKeepsLocationWhenNotFound) {
    const next::Rect64 rect{0, 0, 10, 10};
    auto location = next::internal::rect_location::Left;
    next::Point64 intersection{77, 88};

    const auto found =
        next::internal::get_intersection(rect, {-5, 20}, {-1, 20}, location, intersection);

    EXPECT_FALSE(found);
    EXPECT_EQ(location, next::internal::rect_location::Left);
}

TEST(Clipper2NextRectClipIntersectionsTests, RectIntersectionFindsHorizontalAndVerticalBoundaries) {
    const next::Rect64 rect{0, 0, 10, 10};
    auto left = next::internal::rect_location::Left;
    auto top = next::internal::rect_location::Top;
    next::Point64 horizontal;
    next::Point64 vertical;

    EXPECT_TRUE(next::internal::get_intersection(rect, {-5, 5}, {5, 5}, left, horizontal));
    EXPECT_TRUE(next::internal::get_intersection(rect, {5, -5}, {5, 5}, top, vertical));

    EXPECT_EQ(left, next::internal::rect_location::Left);
    EXPECT_EQ(horizontal, next::Point64(0, 5));
    EXPECT_EQ(top, next::internal::rect_location::Top);
    EXPECT_EQ(vertical, next::Point64(5, 0));
}

TEST(Clipper2NextRectClipIntersectionsTests, RectIntersectionCanUseRectDirectly) {
    const next::Rect64 rect{0, 0, 10, 10};
    auto inside = next::internal::rect_location::Inside;
    auto right = next::internal::rect_location::Right;
    next::Point64 horizontal;
    next::Point64 diagonal;

    EXPECT_TRUE(next::internal::get_intersection(rect, {-5, 5}, {5, 5}, inside, horizontal));
    EXPECT_EQ(inside, next::internal::rect_location::Left);
    EXPECT_EQ(horizontal, next::Point64(0, 5));

    EXPECT_TRUE(next::internal::get_intersection(rect, {15, 15}, {5, 5}, right, diagonal));
    EXPECT_EQ(right, next::internal::rect_location::Right);
    EXPECT_EQ(diagonal, next::Point64(10, 10));
}
