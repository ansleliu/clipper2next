#include "clip/private/boolean_union_service.h"

#include "clipper2next/geometry.h"

#include <algorithm>
#include <cstddef>

#include <gtest/gtest.h>

namespace next = clipper2next;

TEST(UnionComponentGuardTests, CorrectionSingletonFillAndCanonicalDirectionAgreeWithEngine) {
    for (const bool negative : {false, true}) {
        for (const bool reverse : {false, true}) {
            for (const bool preserve : {false, true}) {
                next::Path64 path{{0, 0}, {10, 0}, {10, 10}, {0, 10}};
                if (negative) { std::reverse(path.begin(), path.end()); }
                next::internal::clip_union_options options;
                options.fill_rule = negative ? next::FillRule::Negative : next::FillRule::Positive;
                options.options.reverse_solution = reverse;
                options.options.preserve_collinear = preserve;
                const auto result = next::internal::union_closed_paths(next::Paths64{path}, options);
                EXPECT_DOUBLE_EQ(next::area(result), reverse ? -100 : 100);
            }
        }
    }
}

TEST(UnionComponentGuardTests, CorrectionThinTriangleExactPositiveFill) {
    const next::Path64 triangle{{0, 10000000000LL}, {1000003, 10000000001LL}, {1000002, 10000000001LL}};
    next::internal::clip_union_options options;
    options.fill_rule = next::FillRule::Positive;
    auto path = triangle;
    EXPECT_TRUE(next::internal::try_return_singleton_component_direct(path, options));
    EXPECT_EQ(path, triangle);
}

namespace {

[[nodiscard]] auto negative_square() -> next::Path64 {
    next::Path64 path{{0, 0}, {10, 0}, {10, 10}, {0, 10}};
    if (next::is_positive(path)) { std::reverse(path.begin(), path.end()); }
    return path;
}

[[nodiscard]] auto disjoint_positive_squares(std::size_t count) -> next::Paths64 {
    next::Paths64 paths;
    paths.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        const auto left = static_cast<int64_t>(index * 100U);
        paths.push_back(next::Path64{{left, 0}, {left + 10, 0}, {left + 10, 10}, {left, 10}});
    }
    return paths;
}

}  // namespace

TEST(UnionComponentGuardTests, ReverseSolutionSingletonCanReturnDirectlyWithReversedPath) {
    auto path = negative_square();
    ASSERT_FALSE(next::is_positive(path));

    next::internal::clip_union_options options;
    options.fill_rule = next::FillRule::Negative;
    options.options.reverse_solution = true;

    EXPECT_TRUE(next::internal::try_return_singleton_component_direct(path, options));
    EXPECT_FALSE(next::is_positive(path));
}

TEST(UnionComponentGuardTests, ReverseSolutionSingletonStillRequiresMatchingFillRule) {
    auto path = negative_square();
    ASSERT_FALSE(next::is_positive(path));

    next::internal::clip_union_options options;
    options.fill_rule = next::FillRule::Positive;
    options.options.reverse_solution = true;

    EXPECT_FALSE(next::internal::try_return_singleton_component_direct(path, options));
    EXPECT_FALSE(next::is_positive(path));
}

TEST(UnionComponentGuardTests, SingletonComponentCanAppendDirectResult) {
    auto path = negative_square();
    ASSERT_FALSE(next::is_positive(path));

    next::internal::clip_union_options options;
    options.fill_rule = next::FillRule::Negative;
    options.options.reverse_solution = true;

    next::paths64_result result;
    EXPECT_TRUE(next::internal::try_append_singleton_component_direct(path, options, result));

    ASSERT_EQ(result.closed.size(), 1U);
    EXPECT_FALSE(next::is_positive(result.closed.front()));
    EXPECT_TRUE(result.open.empty());
}

TEST(UnionComponentGuardTests, SingletonComponentTrimsCollinearBeforeDirectResult) {
    next::Path64 path{{0, 0}, {5, 0}, {10, 0}, {10, 10}, {0, 10}};
    ASSERT_TRUE(next::is_positive(path));

    next::internal::clip_union_options options;
    options.fill_rule = next::FillRule::Positive;

    next::paths64_result result;
    EXPECT_TRUE(next::internal::try_append_singleton_component_direct(path, options, result));

    ASSERT_EQ(result.closed.size(), 1U);
    EXPECT_EQ(result.closed.front().size(), 4U);
    EXPECT_TRUE(next::is_positive(result.closed.front()));
}

TEST(UnionComponentGuardTests, ConstUnionUsesComponentDecompositionForDisjointSingletons) {
    const auto paths = disjoint_positive_squares(24U);

    next::internal::clip_union_options options;
    options.fill_rule = next::FillRule::Positive;

    const auto result = next::internal::union_paths(paths, options);

    ASSERT_EQ(result.closed.size(), paths.size());
    EXPECT_TRUE(result.open.empty());
}
