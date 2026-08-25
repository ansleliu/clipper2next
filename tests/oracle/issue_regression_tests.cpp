#include <gtest/gtest.h>

#include "path_equivalence.h"

#include "clipper2/clipper.h"
#include "clipper2next/clipper.h"

namespace legacy = Clipper2Lib;
namespace next = clipper2next;
namespace oracle = clipper2next::tests::oracle;

TEST(Clipper2NextIssueRegressionTests, ClipperIssue720HorizontalSpikesMatchesLegacy) {
    const auto subjects = next::Paths64{
        {{1600, 0}, {1600, 100}, {2050, 100}, {2050, 300}, {450, 300}, {450, 0}},
        {{1800, 200}, {1800, 100}, {1600, 100}, {2000, 100}, {2000, 200}},
    };

    legacy::Clipper64 legacy_clipper;
    legacy_clipper.AddSubject(oracle::to_legacy_paths(subjects));
    legacy::Paths64 expected;
    legacy_clipper.Execute(legacy::ClipType::Union, legacy::FillRule::NonZero, expected);

    next::clip_request64 request;
    request.clip_type = next::ClipType::Union;
    request.fill_rule = next::FillRule::NonZero;
    request.subjects = subjects;
    const auto actual = next::clip(request).closed;

    EXPECT_NO_THROW(oracle::assert_paths_semantically_equal(expected, actual));
}

TEST(Clipper2NextIssueRegressionTests, ClipperIssue777CollinearMacOsCaseMatchesLegacy) {
    const auto subjects = next::Paths64{
        {{0, -453054451}, {0, -433253797}, {-455550000, 0}},
        {{0, -433253797}, {0, 0}, {-455550000, 0}},
    };

    legacy::Clipper64 legacy_clipper;
    legacy_clipper.PreserveCollinear(false);
    legacy_clipper.AddSubject(oracle::to_legacy_paths(subjects));
    legacy::Paths64 expected;
    legacy_clipper.Execute(legacy::ClipType::Union, legacy::FillRule::NonZero, expected);

    next::clip_request64 request;
    request.clip_type = next::ClipType::Union;
    request.fill_rule = next::FillRule::NonZero;
    request.options.preserve_collinear = false;
    request.subjects = subjects;
    const auto actual = next::clip(request).closed;

    EXPECT_NO_THROW(oracle::assert_paths_semantically_equal(expected, actual));
}
