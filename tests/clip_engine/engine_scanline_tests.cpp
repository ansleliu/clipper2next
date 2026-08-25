#include "clip/engine/private/engine_scanline.h"

#include <gtest/gtest.h>

namespace next = clipper2next;

TEST(Clipper2NextEngineScanlineTests, LocalMinimaSortBottomUpThenLeftToRight) {
    next::internal::Vertex top_right{{20, 10}};
    next::internal::Vertex bottom_right{{5, 20}};
    next::internal::Vertex bottom_left{{1, 20}};
    next::internal::LocalMinimaList minima;
    minima.emplace_back(top_right, next::PathType::Subject, false);
    minima.emplace_back(bottom_right, next::PathType::Subject, false);
    minima.emplace_back(bottom_left, next::PathType::Subject, false);

    next::internal::sort_local_minima(minima);

    ASSERT_EQ(minima.size(), 3U);
    EXPECT_EQ(minima[0].vertex.get().pt, bottom_left.pt);
    EXPECT_EQ(minima[1].vertex.get().pt, bottom_right.pt);
    EXPECT_EQ(minima[2].vertex.get().pt, top_right.pt);
}

TEST(Clipper2NextEngineScanlineTests, ScanlineQueuePopsHighestAndSkipsDuplicates) {
    next::internal::scanline_queue scanlines;
    next::internal::push_scanline(scanlines, 10);
    next::internal::push_scanline(scanlines, 30);
    next::internal::push_scanline(scanlines, 30);
    next::internal::push_scanline(scanlines, 20);

    int64_t y = 0;
    ASSERT_TRUE(next::internal::pop_scanline(scanlines, y));
    EXPECT_EQ(y, 30);

    ASSERT_TRUE(next::internal::pop_scanline(scanlines, y));
    EXPECT_EQ(y, 20);

    ASSERT_TRUE(next::internal::pop_scanline(scanlines, y));
    EXPECT_EQ(y, 10);

    EXPECT_FALSE(next::internal::pop_scanline(scanlines, y));
}

TEST(Clipper2NextEngineScanlineTests, ScanlineQueueClearKeepsReservedStorage) {
    next::internal::scanline_queue scanlines;
    scanlines.reserve(16);
    next::internal::push_scanline(scanlines, 10);
    next::internal::push_scanline(scanlines, 20);

    scanlines.clear();

    EXPECT_TRUE(scanlines.empty());
    EXPECT_GE(scanlines.capacity(), 16U);
}

TEST(Clipper2NextEngineScanlineTests, ResetScanlinesDeduplicatesSortedLocalMinimaY) {
    next::internal::Vertex high_left{{0, 30}};
    next::internal::Vertex high_right{{10, 30}};
    next::internal::Vertex low{{0, 10}};
    next::internal::LocalMinimaList minima;
    minima.emplace_back(high_left, next::PathType::Subject, false);
    minima.emplace_back(high_right, next::PathType::Subject, false);
    minima.emplace_back(low, next::PathType::Subject, false);

    next::internal::scanline_queue scanlines;
    next::internal::reset_scanlines(scanlines, minima);

    EXPECT_EQ(scanlines.size(), 2U);

    int64_t y = 0;
    ASSERT_TRUE(next::internal::pop_scanline(scanlines, y));
    EXPECT_EQ(y, 30);
    ASSERT_TRUE(next::internal::pop_scanline(scanlines, y));
    EXPECT_EQ(y, 10);
    EXPECT_FALSE(next::internal::pop_scanline(scanlines, y));
}

TEST(Clipper2NextEngineScanlineTests, LocalMinimaPopAdvancesMatchingScanline) {
    next::internal::Vertex first_vertex{{0, 20}};
    next::internal::Vertex second_vertex{{0, 10}};
    next::internal::LocalMinimaList minima;
    minima.emplace_back(first_vertex, next::PathType::Subject, false);
    minima.emplace_back(second_vertex, next::PathType::Subject, false);
    auto current = minima.begin();
    next::internal::local_minimum_node* local_minima = nullptr;

    EXPECT_TRUE(next::internal::pop_local_minima(current, minima.end(), 20, local_minima));
    EXPECT_EQ(&local_minima->vertex.get(), &first_vertex);
    EXPECT_EQ(current, minima.begin() + 1);

    EXPECT_FALSE(next::internal::pop_local_minima(current, minima.end(), 20, local_minima));
    EXPECT_TRUE(next::internal::pop_local_minima(current, minima.end(), 10, local_minima));
    EXPECT_EQ(&local_minima->vertex.get(), &second_vertex);
    EXPECT_EQ(current, minima.end());
}
