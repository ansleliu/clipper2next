#include "clipper2next/clip.h"
#include "clip/private/closed_clip_fast_path.h"
#include "clip/private/clip_execution_strategy.h"

#include <gtest/gtest.h>

#include <utility>
#include <vector>

namespace next = clipper2next;

namespace {

[[nodiscard]] auto rectangle_path(int64_t left, int64_t top, int64_t right, int64_t bottom)
    -> next::Path64 {
    return {{left, top}, {right, top}, {right, bottom}, {left, bottom}};
}

auto expect_rectangle_fast_path_matches_engine(next::ClipType clip_type,
                                               const next::Path64& subject,
                                               const next::Path64& clip) -> void {
    next::clip_request64 request;
    request.clip_type = clip_type;
    request.fill_rule = next::FillRule::NonZero;
    request.subjects = {subject};
    request.clips = {clip};

    next::paths64_result result;
    ASSERT_TRUE(next::internal::try_execute_closed_clip_fast_path(request, result))
        << "clip_type=" << static_cast<int>(clip_type);
    const auto full_engine_result = next::internal::execute_clip_validated(request);
    EXPECT_EQ(result.closed, full_engine_result.closed);
    EXPECT_TRUE(result.open.empty());
}

}  // namespace

TEST(Clipper2NextClosedClipFastPathTests, DefaultClosedClipRequestUsesFastPathSemantics) {
    next::clip_request64 request;
    request.clip_type = next::ClipType::Union;
    request.fill_rule = next::FillRule::NonZero;
    request.subjects = {{{0, 0}, {100, 0}, {100, 100}, {0, 100}}};
    request.clips = {{{50, 50}, {150, 50}, {150, 150}, {50, 150}}};

    next::paths64_result result;
    ASSERT_TRUE(next::internal::try_execute_closed_clip_fast_path(request, result));

    EXPECT_EQ(result.closed, next::internal::execute_clip_validated(request).closed);
    EXPECT_TRUE(result.open.empty());
}

TEST(Clipper2NextClosedClipFastPathTests, RectangleUnionFastPathMatchesFullEngineForDiagonalOverlaps) {
    const std::vector<std::pair<next::Path64, next::Path64>> cases{
        {
            {{0, 0}, {100, 0}, {100, 100}, {0, 100}},
            {{50, 50}, {150, 50}, {150, 150}, {50, 150}},
        },
        {
            {{50, 50}, {150, 50}, {150, 150}, {50, 150}},
            {{0, 0}, {100, 0}, {100, 100}, {0, 100}},
        },
        {
            {{0, 50}, {100, 50}, {100, 150}, {0, 150}},
            {{50, 0}, {150, 0}, {150, 100}, {50, 100}},
        },
        {
            {{50, 0}, {150, 0}, {150, 100}, {50, 100}},
            {{0, 50}, {100, 50}, {100, 150}, {0, 150}},
        },
    };

    for (const auto& [subject, clip] : cases) {
        next::clip_request64 request;
        request.clip_type = next::ClipType::Union;
        request.fill_rule = next::FillRule::NonZero;
        request.subjects = {subject};
        request.clips = {clip};

        next::paths64_result result;
        ASSERT_TRUE(next::internal::try_execute_closed_clip_fast_path(request, result));
        EXPECT_EQ(result.closed, next::internal::execute_clip_validated(request).closed);
        EXPECT_TRUE(result.open.empty());
    }
}

TEST(Clipper2NextClosedClipFastPathTests, RectangleIntersectionAndDifferenceFastPathsMatchFullEngine) {
    const std::vector<std::pair<next::Path64, next::Path64>> cases{
        {
            {{0, 0}, {100, 0}, {100, 100}, {0, 100}},
            {{50, 50}, {150, 50}, {150, 150}, {50, 150}},
        },
        {
            {{0, 0}, {100, 0}, {100, 100}, {0, 100}},
            {{50, -50}, {150, -50}, {150, 50}, {50, 50}},
        },
        {
            {{0, 0}, {100, 0}, {100, 100}, {0, 100}},
            {{-50, -50}, {50, -50}, {50, 50}, {-50, 50}},
        },
        {
            {{0, 0}, {100, 0}, {100, 100}, {0, 100}},
            {{-50, 50}, {50, 50}, {50, 150}, {-50, 150}},
        },
    };

    for (const auto clip_type : {next::ClipType::Intersection, next::ClipType::Difference}) {
        for (const auto& [subject, clip] : cases) {
            next::clip_request64 request;
            request.clip_type = clip_type;
            request.fill_rule = next::FillRule::NonZero;
            request.subjects = {subject};
            request.clips = {clip};

            next::paths64_result result;
            ASSERT_TRUE(next::internal::try_execute_closed_clip_fast_path(request, result));
            EXPECT_EQ(result.closed, next::internal::execute_clip_validated(request).closed);
            EXPECT_TRUE(result.open.empty());
        }
    }
}

TEST(Clipper2NextClosedClipFastPathTests, ContainingRectangleIntersectionUsesFastPathSemantics) {
    next::clip_request64 request;
    request.clip_type = next::ClipType::Intersection;
    request.fill_rule = next::FillRule::NonZero;
    request.subjects = {
        {{10, 10}, {90, 10}, {90, 90}, {10, 90}},
        {{30, 30}, {30, 70}, {70, 70}, {70, 30}},
    };
    request.clips = {{{0, 0}, {100, 0}, {100, 100}, {0, 100}}};

    next::paths64_result result;
    ASSERT_TRUE(next::internal::try_execute_closed_clip_fast_path(request, result));
    const auto full_engine_result = next::internal::execute_clip_validated(request);
    ASSERT_EQ(result.closed.size(), full_engine_result.closed.size());
    EXPECT_NEAR(next::area(result.closed), next::area(full_engine_result.closed), 0.001);
    EXPECT_TRUE(result.open.empty());
}

TEST(Clipper2NextClosedClipFastPathTests, ContainingRectangleIntersectionNormalizesOuterOrientation) {
    next::clip_request64 request;
    request.clip_type = next::ClipType::Intersection;
    request.fill_rule = next::FillRule::NonZero;
    request.subjects = {{{10, 10}, {10, 90}, {90, 90}, {90, 10}}};
    request.clips = {{{0, 0}, {100, 0}, {100, 100}, {0, 100}}};

    next::paths64_result result;
    ASSERT_TRUE(next::internal::try_execute_closed_clip_fast_path(request, result));
    const auto full_engine_result = next::internal::execute_clip_validated(request);
    ASSERT_EQ(result.closed.size(), full_engine_result.closed.size());
    EXPECT_EQ(next::area(result.closed), next::area(full_engine_result.closed));
    EXPECT_GT(next::area(result.closed), 0.0);
}

TEST(Clipper2NextClosedClipFastPathTests, LargeContainingRectangleIntersectionUsesFastPathWhenSafe) {
    next::Path64 subject;
    subject.reserve(300U);
    for (int index = 0; index < 150; ++index) {
        subject.push_back(next::Point64{-1500 + index * 20, 1000 + (index % 2)});
    }
    for (int index = 149; index >= 0; --index) {
        subject.push_back(next::Point64{-1500 + index * 20, -1000 - (index % 2)});
    }

    next::clip_request64 request;
    request.clip_type = next::ClipType::Intersection;
    request.fill_rule = next::FillRule::NonZero;
    request.subjects = {subject};
    request.clips = {{{-2000, -2000}, {2000, -2000}, {2000, 2000}, {-2000, 2000}}};

    next::paths64_result result;
    ASSERT_TRUE(next::internal::try_execute_closed_clip_fast_path(request, result));
    const auto full_engine_result = next::internal::execute_clip_validated(request);
    ASSERT_EQ(result.closed.size(), full_engine_result.closed.size());
    EXPECT_EQ(next::area(result.closed), next::area(full_engine_result.closed));
}

TEST(Clipper2NextClosedClipFastPathTests, ContainingRectangleIntersectionCleansPreservedCollinearSpikes) {
    next::Path64 subject;
    subject.reserve(304U);
    for (int index = 0; index < 300; ++index) { subject.push_back(next::Point64{index * 10, 0}); }
    subject.push_back(next::Point64{2980, 0});
    subject.push_back(next::Point64{2990, 0});
    subject.push_back(next::Point64{2990, 1000});
    subject.push_back(next::Point64{0, 1000});

    next::clip_request64 request;
    request.clip_type = next::ClipType::Intersection;
    request.fill_rule = next::FillRule::NonZero;
    request.subjects = {subject};
    request.clips = {{{-10, -10}, {3000, -10}, {3000, 1010}, {-10, 1010}}};

    next::paths64_result result;
    ASSERT_TRUE(next::internal::try_execute_closed_clip_fast_path(request, result));
    const auto full_engine_result = next::internal::execute_clip_validated(request);
    ASSERT_EQ(result.closed.size(), full_engine_result.closed.size());
    ASSERT_EQ(result.closed.front().size(), full_engine_result.closed.front().size());
    EXPECT_EQ(next::area(result.closed), next::area(full_engine_result.closed));
}

TEST(Clipper2NextClosedClipFastPathTests, ContainingRectangleIntersectionAbovePointCapBypassesFastPath) {
    next::Path64 subject;
    subject.reserve(4100U);
    for (int index = 0; index < 2050; ++index) {
        subject.push_back(next::Point64{-30000 + index * 20, 1000 + (index % 2)});
    }
    for (int index = 2049; index >= 0; --index) {
        subject.push_back(next::Point64{-30000 + index * 20, -1000 - (index % 2)});
    }

    next::clip_request64 request;
    request.clip_type = next::ClipType::Intersection;
    request.fill_rule = next::FillRule::NonZero;
    request.subjects = {subject};
    request.clips = {{{-40000, -2000}, {40000, -2000}, {40000, 2000}, {-40000, 2000}}};

    next::paths64_result result;
    EXPECT_FALSE(next::internal::try_execute_closed_clip_fast_path(request, result));
}

TEST(Clipper2NextClosedClipFastPathTests, TwoRectangleIntersectionUsesStrictFastPath) {
    next::clip_request64 request;
    request.clip_type = next::ClipType::Intersection;
    request.fill_rule = next::FillRule::NonZero;
    request.subjects = {{{10, 10}, {90, 10}, {90, 90}, {10, 90}}};
    request.clips = {{{50, 50}, {100, 50}, {100, 100}, {50, 100}}};

    next::paths64_result result;
    ASSERT_TRUE(next::internal::try_execute_closed_clip_fast_path(request, result));
    EXPECT_EQ(result.closed, next::internal::execute_clip_validated(request).closed);
    EXPECT_TRUE(result.open.empty());
}

TEST(Clipper2NextClosedClipFastPathTests, OverlappingContainedSubjectsBypassFastPath) {
    next::clip_request64 request;
    request.clip_type = next::ClipType::Intersection;
    request.fill_rule = next::FillRule::NonZero;
    request.subjects = {
        {{10, 10}, {70, 10}, {70, 70}, {10, 70}},
        {{40, 40}, {90, 40}, {90, 90}, {40, 90}},
    };
    request.clips = {{{0, 0}, {100, 0}, {100, 100}, {0, 100}}};

    next::paths64_result result;
    EXPECT_FALSE(next::internal::try_execute_closed_clip_fast_path(request, result));
}

TEST(Clipper2NextClosedClipFastPathTests, EdgeTouchingRectanglesUseClosedClipFastPath) {
    const auto left = rectangle_path(0, 0, 100, 100);
    const auto right = rectangle_path(100, 0, 200, 100);
    const auto top = rectangle_path(0, 0, 100, 100);
    const auto bottom = rectangle_path(0, 100, 100, 200);

    for (const auto clip_type : {next::ClipType::Intersection,
                                 next::ClipType::Union,
                                 next::ClipType::Difference,
                                 next::ClipType::Xor}) {
        expect_rectangle_fast_path_matches_engine(clip_type, left, right);
        expect_rectangle_fast_path_matches_engine(clip_type, top, bottom);
    }
}

TEST(Clipper2NextClosedClipFastPathTests, PartialEdgeTouchingRectangleUnionUsesFullEngine) {
    next::clip_request64 request;
    request.clip_type = next::ClipType::Union;
    request.fill_rule = next::FillRule::NonZero;
    request.subjects = {rectangle_path(0, 0, 75000, 75000)};
    request.clips = {rectangle_path(75000, 25000, 100000, 50000)};

    next::paths64_result fast_result;
    EXPECT_FALSE(next::internal::try_execute_closed_clip_fast_path(request, fast_result));
    EXPECT_EQ(next::clip(request).closed, next::internal::execute_clip_validated(request).closed);
}

TEST(Clipper2NextClosedClipFastPathTests, PartialEdgeTouchingRectangleXorUsesFullEngine) {
    next::clip_request64 request;
    request.clip_type = next::ClipType::Xor;
    request.fill_rule = next::FillRule::NonZero;
    request.subjects = {rectangle_path(0, 0, 75000, 75000)};
    request.clips = {rectangle_path(75000, 25000, 100000, 50000)};

    next::paths64_result fast_result;
    EXPECT_FALSE(next::internal::try_execute_closed_clip_fast_path(request, fast_result));
    EXPECT_EQ(next::clip(request).closed, next::internal::execute_clip_validated(request).closed);
}

TEST(Clipper2NextClosedClipFastPathTests, DisjointRectanglesUseClosedClipFastPath) {
    const auto subject = rectangle_path(0, 0, 100, 100);
    const auto clip = rectangle_path(200, 0, 300, 100);

    for (const auto clip_type : {next::ClipType::Intersection,
                                 next::ClipType::Union,
                                 next::ClipType::Difference,
                                 next::ClipType::Xor}) {
        expect_rectangle_fast_path_matches_engine(clip_type, subject, clip);
    }
}

TEST(Clipper2NextClosedClipFastPathTests, NonRectangularEmptyIntersectionUsesClosedClipFastPath) {
    const std::vector<std::pair<next::Path64, next::Path64>> cases{
        {
            {{0, 0}, {10, 0}, {0, 10}},
            {{20, 0}, {30, 0}, {20, 10}},
        },
        {
            {{0, 0}, {10, 0}, {10, 10}},
            {{10, 0}, {20, 5}, {10, 10}},
        },
    };

    for (const auto& [subject, clip] : cases) {
        next::clip_request64 request;
        request.clip_type = next::ClipType::Intersection;
        request.fill_rule = next::FillRule::NonZero;
        request.subjects = {subject};
        request.clips = {clip};

        next::paths64_result result;
        ASSERT_TRUE(next::internal::try_execute_closed_clip_fast_path(request, result));
        EXPECT_EQ(result.closed, next::internal::execute_clip_validated(request).closed);
        EXPECT_TRUE(result.open.empty());
    }
}

TEST(Clipper2NextClosedClipFastPathTests, NonDefaultClipRequestOptionsBypassFastPath) {
    next::clip_request64 request;
    request.clip_type = next::ClipType::Union;
    request.fill_rule = next::FillRule::NonZero;
    request.options.reverse_solution = true;
    request.subjects = {{{0, 0}, {100, 0}, {100, 100}, {0, 100}}};
    request.clips = {{{50, 50}, {150, 50}, {150, 150}, {50, 150}}};

    next::paths64_result result;
    EXPECT_FALSE(next::internal::try_execute_closed_clip_fast_path(request, result));
}

TEST(Clipper2NextClosedClipFastPathTests, ExplicitNoPreserveCollinearBypassesFastPath) {
    next::clip_request64 request;
    request.clip_type = next::ClipType::Union;
    request.fill_rule = next::FillRule::NonZero;
    request.options.preserve_collinear = false;
    request.subjects = {{{0, 0}, {100, 0}, {100, 100}, {0, 100}}};
    request.clips = {{{50, 50}, {150, 50}, {150, 150}, {50, 150}}};

    next::paths64_result result;
    EXPECT_FALSE(next::internal::try_execute_closed_clip_fast_path(request, result));
}

TEST(Clipper2NextClosedClipFastPathTests, OpenSubjectBypassesClosedClipFastPath) {
    next::clip_request64 request;
    request.clip_type = next::ClipType::Intersection;
    request.fill_rule = next::FillRule::EvenOdd;
    request.open_subjects = {{{0, 50}, {100, 50}}};
    request.clips = {{{25, 25}, {75, 25}, {75, 75}, {25, 75}}};

    next::paths64_result result;
    EXPECT_FALSE(next::internal::try_execute_closed_clip_fast_path(request, result));
}

TEST(Clipper2NextClosedClipFastPathTests, NoClipRequestBypassesFastPath) {
    next::clip_request64 request;
    request.clip_type = next::ClipType::NoClip;

    next::paths64_result result;
    EXPECT_FALSE(next::internal::try_execute_closed_clip_fast_path(request, result));
}

TEST(Clipper2NextClosedClipFastPathTests, BowtieVertexOrderIsNotTreatedAsRectangle) {
    // Contains all four corners of [0,100]x[0,100] but in a self-intersecting
    // diagonal order; the engine resolves it into two triangles.
    const next::Path64 bowtie{{0, 0}, {100, 100}, {100, 0}, {0, 100}};

    for (const auto clip_type :
         {next::ClipType::Intersection, next::ClipType::Union, next::ClipType::Difference}) {
        next::clip_request64 request;
        request.clip_type = clip_type;
        request.fill_rule = next::FillRule::NonZero;
        request.subjects = {bowtie};
        request.clips = {{{-50, -50}, {150, -50}, {150, 150}, {-50, 150}}};

        next::paths64_result fast_result;
        EXPECT_FALSE(next::internal::try_execute_closed_clip_fast_path(request, fast_result));

        const auto public_result = next::clip(request);
        const auto engine_result = next::internal::execute_clip_validated(request);
        EXPECT_EQ(public_result.closed, engine_result.closed);
    }
}

TEST(Clipper2NextClosedClipFastPathTests, OrientationSensitiveFillRulesBypassFastPath) {
    // A clockwise rectangle is empty under Positive fill; the rectangle fast
    // paths ignore winding direction so they must decline these fill rules.
    const next::Path64 clockwise_subject{{0, 0}, {0, 100}, {100, 100}, {100, 0}};

    for (const auto fill_rule : {next::FillRule::Positive, next::FillRule::Negative}) {
        next::clip_request64 request;
        request.clip_type = next::ClipType::Intersection;
        request.fill_rule = fill_rule;
        request.subjects = {clockwise_subject};
        request.clips = {{{50, 50}, {150, 50}, {150, 150}, {50, 150}}};

        next::paths64_result fast_result;
        EXPECT_FALSE(next::internal::try_execute_closed_clip_fast_path(request, fast_result));

        const auto public_result = next::clip(request);
        const auto engine_result = next::internal::execute_clip_validated(request);
        EXPECT_EQ(public_result.closed, engine_result.closed);
    }
}

TEST(Clipper2NextClosedClipFastPathTests,
     NonZeroNestedSameWindingRingsMatchEngineThroughPublicApi) {
    // Both rings wound the same way: under NonZero the engine absorbs the
    // inner ring instead of turning it into a hole, so passthrough must not
    // reorient it by containment parity.
    next::clip_request64 request;
    request.clip_type = next::ClipType::Intersection;
    request.fill_rule = next::FillRule::NonZero;
    request.subjects = {
        {{10, 10}, {90, 10}, {90, 90}, {10, 90}},
        {{30, 30}, {70, 30}, {70, 70}, {30, 70}},
    };
    request.clips = {{{0, 0}, {100, 0}, {100, 100}, {0, 100}}};

    const auto public_result = next::clip(request);
    const auto engine_result = next::internal::execute_clip_validated(request);
    ASSERT_EQ(public_result.closed.size(), engine_result.closed.size());
    EXPECT_EQ(next::area(public_result.closed), next::area(engine_result.closed));
}

TEST(Clipper2NextClosedClipFastPathTests, EvenOddNestedSameWindingRingsMatchEngine) {
    // Under EvenOdd the same input is a donut for the engine, and parity
    // reorientation in the passthrough remains valid.
    next::clip_request64 request;
    request.clip_type = next::ClipType::Intersection;
    request.fill_rule = next::FillRule::EvenOdd;
    request.subjects = {
        {{10, 10}, {90, 10}, {90, 90}, {10, 90}},
        {{30, 30}, {70, 30}, {70, 70}, {30, 70}},
    };
    request.clips = {{{0, 0}, {100, 0}, {100, 100}, {0, 100}}};

    const auto public_result = next::clip(request);
    const auto engine_result = next::internal::execute_clip_validated(request);
    ASSERT_EQ(public_result.closed.size(), engine_result.closed.size());
    EXPECT_EQ(next::area(public_result.closed), next::area(engine_result.closed));
}

TEST(Clipper2NextClosedClipFastPathTests, SelfIntersectingContainedSubjectBypassesPassthrough) {
    // An asymmetric bowtie has nonzero area, so it passes the trivial area
    // guard; only an explicit simplicity check keeps it off the passthrough.
    next::clip_request64 request;
    request.clip_type = next::ClipType::Intersection;
    request.fill_rule = next::FillRule::NonZero;
    request.subjects = {{{10, 10}, {90, 80}, {90, 10}, {10, 80}}};
    request.clips = {{{0, 0}, {100, 0}, {100, 100}, {0, 100}}};

    next::paths64_result fast_result;
    EXPECT_FALSE(next::internal::try_execute_closed_clip_fast_path(request, fast_result));

    const auto public_result = next::clip(request);
    const auto engine_result = next::internal::execute_clip_validated(request);
    EXPECT_EQ(public_result.closed, engine_result.closed);
}

TEST(Clipper2NextClosedClipFastPathTests, ReleaseThreadCachesKeepsSubsequentClipsWorking) {
    next::clip_request64 request;
    request.clip_type = next::ClipType::Union;
    request.fill_rule = next::FillRule::NonZero;
    request.subjects = {{{0, 0}, {100, 0}, {100, 100}, {0, 100}}};
    request.clips = {{{50, 50}, {150, 50}, {150, 150}, {50, 150}}};

    const auto before = next::internal::execute_clip_validated(request);
    next::release_thread_caches();
    const auto after = next::internal::execute_clip_validated(request);
    EXPECT_EQ(before.closed, after.closed);
}
