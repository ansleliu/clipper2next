#include <gtest/gtest.h>

#include "wkt_parser.h"

#include "clipper2next/clipper.h"

namespace next = clipper2next;
namespace oracle = clipper2next::tests::oracle;

TEST(Clipper2NextExternalCorpusTests, ParsesLinearWktForOpenLineCorpus) {
    const auto lines = oracle::parse_linear_wkt("MULTILINESTRING ((0 0, 10 10), (20 0, 20 10))",
                                                oracle::wkt_parser_options{.scale = 10.0L});

    ASSERT_EQ(lines.size(), 2U);
    ASSERT_EQ(lines[0].size(), 2U);
    const next::Point64 expected_first_line_end{100, 100};
    const next::Point64 expected_second_line_begin{200, 0};
    EXPECT_EQ(lines[0][1], expected_first_line_end);
    EXPECT_EQ(lines[1][0], expected_second_line_begin);
}
