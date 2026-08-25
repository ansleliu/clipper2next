#include <algorithm>
#include <cstddef>
#include <vector>

#include <gtest/gtest.h>

#include "clipper2next/clipper.h"
#include "support/test_paths.h"

namespace next = clipper2next;
namespace test = clipper2next::tests;

namespace {

auto lexicographically_less(const next::Point64& lhs, const next::Point64& rhs) -> bool {
    return lhs.x < rhs.x || (lhs.x == rhs.x && lhs.y < rhs.y);
}

auto canonicalize_closed_path(next::Path64 path) -> next::Path64 {
    if (path.empty()) { return path; }

    const auto first = std::min_element(path.begin(), path.end(), lexicographically_less);
    std::rotate(path.begin(), first, path.end());
    return path;
}

}  // namespace

TEST(Clipper2NextCanonicalizeTests, RotatedClosedPathCanBeCanonicalizedForExplicitComparisons) {
    auto path = test::path64({10, 10, 20, 10, 20, 20, 10, 20});
    auto rotated = test::path64({20, 20, 10, 20, 10, 10, 20, 10});

    EXPECT_NE(path, rotated);
    EXPECT_EQ(canonicalize_closed_path(path), canonicalize_closed_path(rotated));
}
