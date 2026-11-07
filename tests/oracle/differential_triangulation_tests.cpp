#include <gtest/gtest.h>
#include "support/triangulation_contract.h"
#include <algorithm>
#include <iostream>
namespace next = clipper2next;
namespace oracle = clipper2next::tests::oracle;

TEST(Clipper2NextDifferentialTriangulationTests, ActualBoundaryContractsRecordBothEngines) {
    const std::vector<next::Paths64> cases{
        {},
        {{{0, 0}, {100, 100}}},
        {{{0, 0}, {120, 0}, {120, 80}, {0, 80}}},
        {{{0, 0}, {1, 0}, {0, 5}}},
        {{{0, 0}, {100, 0}, {100, 100}, {0, 100}}, {{50, 50}, {50, 51}, {55, 50}}},
        {{{0, 0}, {10, 0}, {10, 10}, {0, 10}}, {{10, 10}, {20, 10}, {20, 20}, {10, 20}}},
        {{{0, 0}, {10, 0}, {10, 10}, {0, 10}}, {{10, 3}, {20, 3}, {20, 7}, {10, 7}}},
        {{{0, 0}, {120, 120}, {0, 120}, {120, 0}}},
        {{{0, 0}, {1, 1}, {0, 1}, {1, 0}}},
        {{{0, 0}, {160, 0}, {160, 50}, {90, 50}, {90, 110}, {0, 110}}},
        {{{-80, -30}, {80, -40}, {140, 40}, {50, 120}, {-90, 80}}},
        {{{0, 0}, {300, 0}, {300, 260}, {0, 260}}, {{80, 80}, {80, 180}, {220, 180}, {220, 80}}},
        // Invalid nested orientation and mixed root orientation remain explicit errors.
        {{{0, 0}, {10, 0}, {10, 10}, {0, 10}}, {{2, 2}, {8, 2}, {8, 8}, {2, 8}}},
        {{{0, 0}, {10, 0}, {10, 10}, {0, 10}}, {{20, 0}, {20, 10}, {30, 10}, {30, 0}}},
    };
    std::size_t index = 0;
    for (const auto& paths : cases) {
        for (bool delaunay : {false, true}) {
            oracle::record_triangulation_case(std::cout, index++, delaunay, paths);
        }
    }
    for (std::size_t outer_start = 0; outer_start < 4; ++outer_start) {
        for (std::size_t hole_start = 0; hole_start < 3; ++hole_start) {
            for (bool swap : {false, true}) {
                for (bool reverse : {false, true}) {
                    next::Paths64 paths{{{0, 0}, {10, 0}, {10, 10}, {0, 10}},
                                        {{0, 5}, {2, 7}, {2, 3}}};
                    std::rotate(paths[0].begin(), paths[0].begin() + outer_start, paths[0].end());
                    std::rotate(paths[1].begin(), paths[1].begin() + hole_start, paths[1].end());
                    if (swap) { std::swap(paths[0], paths[1]); }
                    if (reverse) {
                        for (auto& path : paths) { std::reverse(path.begin(), path.end()); }
                    }
                    for (bool delaunay : {false, true}) {
                        oracle::record_triangulation_case(std::cout, index++, delaunay, paths);
                    }
                }
            }
        }
    }
    ASSERT_EQ(index, 124U);
    std::cout << "Triangulation records=READY cases=" << index << " mathematical_verdict=PENDING\n";
}
