#include "clipper2next/api/memory.h"
#include "clipper2next/clip.h"
#include "clipper2next/offset.h"
#include "clipper2next/rectclip.h"
#include "clipper2next/triangulation.h"
#include "offset/private/offset_state.h"
#include "rectclip/private/rectclip_context.h"
#include "rectclip/private/rectclip_thread_state.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <type_traits>

namespace next = clipper2next;

TEST(Clipper2NextMemoryApiTests, ReleaseThreadCachesKeepsNoexceptPublicContract) {
    static_assert(noexcept(next::release_thread_caches()));
    EXPECT_TRUE((std::is_nothrow_invocable_v<decltype(next::release_thread_caches)>));
}

TEST(Clipper2NextMemoryApiTests, ReleaseThreadCachesCanBeCalledAfterPublicGeometryWork) {
    const next::Paths64 polygon{
        next::Path64{{0, 0}, {100, 0}, {100, 100}, {0, 100}},
    };

    next::clip_request64 clip_request;
    clip_request.clip_type = next::ClipType::Union;
    clip_request.fill_rule = next::FillRule::NonZero;
    clip_request.subjects = polygon;
    EXPECT_FALSE(next::clip(clip_request).closed.empty());

    next::offset_request64 offset_request;
    offset_request.paths = polygon;
    offset_request.delta = 2.0;
    EXPECT_FALSE(next::offset(offset_request).closed.empty());

    next::rect_clip_request64 rect_request;
    rect_request.rect = next::Rect64{10, 10, 90, 90};
    rect_request.paths = polygon;
    EXPECT_FALSE(next::rect_clip(rect_request).paths.empty());

    next::triangulation_request64 triangulation_request;
    triangulation_request.paths = polygon;
    EXPECT_EQ(next::triangulate(triangulation_request).status,
              next::TriangulateResult::success);

    for (int index = 0; index < 4; ++index) { next::release_thread_caches(); }
}

TEST(Clipper2NextMemoryApiTests, ReleaseThreadCachesReturnsRectClipRetainedStorage) {
    auto& context = next::internal::acquire_reusable_rectclip_context({0, 0, 10, 10});
    for (std::size_t index = 0; index < 1025U; ++index) {
        static_cast<void>(context.op_container.emplace());
    }
    context.results.reserve(64U);
    context.edges[0].reserve(64U);
    context.start_locs.reserve(64U);

    ASSERT_GT(context.op_container.retained_capacity(), 0U);
    ASSERT_GT(context.results.capacity(), 0U);
    ASSERT_GT(context.edges[0].capacity(), 0U);
    ASSERT_GT(context.start_locs.capacity(), 0U);

    next::release_thread_caches();

    EXPECT_EQ(context.op_container.retained_capacity(), 0U);
    EXPECT_EQ(context.results.capacity(), 0U);
    EXPECT_EQ(context.edges[0].capacity(), 0U);
    EXPECT_EQ(context.start_locs.capacity(), 0U);
}

TEST(Clipper2NextMemoryApiTests, OffsetStateReleaseReturnsRetainedStorage) {
    next::internal::offset_state state;
    state.normals.reserve(64U);
    state.path_out.reserve(64U);

    ASSERT_GT(state.normals.capacity(), 0U);
    ASSERT_GT(state.path_out.capacity(), 0U);

    state.release();

    EXPECT_EQ(state.normals.capacity(), 0U);
    EXPECT_EQ(state.path_out.capacity(), 0U);
}
