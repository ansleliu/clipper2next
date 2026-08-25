#include "support/private/storage/stable_pool.h"

#include <gtest/gtest.h>

namespace next = clipper2next;

TEST(Clipper2NextStablePoolTests, KeepsReferencesStableAcrossInsertions) {
    next::internal::stable_pool<int> pool;

    int& first = pool.emplace(7);
    auto* first_address = &first;
    for (int index = 0; index < 1024; ++index) { static_cast<void>(pool.emplace(index)); }

    EXPECT_EQ(first_address, &first);
    EXPECT_EQ(*first_address, 7);
    EXPECT_EQ(pool.size(), 1025U);
}

TEST(Clipper2NextStablePoolTests, ClearReleasesTrackedEntries) {
    next::internal::stable_pool<int> pool;
    static_cast<void>(pool.emplace(1));
    static_cast<void>(pool.emplace(2));

    pool.clear();

    EXPECT_TRUE(pool.empty());
    EXPECT_EQ(pool.size(), 0U);
}

TEST(Clipper2NextStablePoolTests, ReleaseReturnsRetainedBlocks) {
    next::internal::stable_pool<int, 2U> pool;
    static_cast<void>(pool.emplace(1));
    static_cast<void>(pool.emplace(2));
    static_cast<void>(pool.emplace(3));
    ASSERT_GE(pool.retained_capacity(), 3U);

    pool.release();

    EXPECT_EQ(pool.retained_capacity(), 0U);
}
