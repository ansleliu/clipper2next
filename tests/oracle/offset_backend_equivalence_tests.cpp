#include <gtest/gtest.h>

#include "support/path_equivalence.h"

#include "clipper2next/clipper.h"
#include "offset/private/offset_algorithm.h"
#include "offset/private/offset_group.h"

#include <array>
#include <cstdint>
#include <utility>
#include <vector>

namespace next = clipper2next;
namespace oracle = clipper2next::tests::oracle;

namespace {

[[nodiscard]] auto closed_subject(std::int64_t offset) -> next::Paths64 {
    return next::Paths64{{{offset, 0},
                          {offset + 140, 0},
                          {offset + 180, 60},
                          {offset + 110, 130},
                          {offset + 40, 110},
                          {offset - 20, 50}}};
}

[[nodiscard]] auto open_subject(std::int64_t offset) -> next::Paths64 {
    return next::Paths64{{{offset, 0},
                          {offset + 60, 20},
                          {offset + 110, -30},
                          {offset + 170, 40},
                          {offset + 230, 15}}};
}

[[nodiscard]] auto make_request(next::Paths64 paths,
                                double delta,
                                next::JoinType join_type,
                                next::EndType end_type,
                                double arc_tolerance,
                                double miter_limit) -> next::offset_request64 {
    next::offset_request64 request;
    request.paths = std::move(paths);
    request.delta = delta;
    request.join_type = join_type;
    request.end_type = end_type;
    request.arc_tolerance = arc_tolerance;
    request.miter_limit = miter_limit;
    return request;
}

[[nodiscard]] auto execute_scalar_reference_offset(const next::offset_request64& request)
    -> next::Paths64 {
    next::internal::offset_state state;
    std::vector<next::internal::offset_group> groups;
    if (!request.paths.empty()) {
        groups.emplace_back(request.paths, request.join_type, request.end_type);
    }

    next::Paths64 result;
    next::internal::execute_offset_algorithm_scalar_reference(
        state,
        groups,
        request.delta,
        result,
        nullptr,
        next::internal::offset_algorithm_options{
            .miter_limit = request.miter_limit,
            .arc_tolerance = request.arc_tolerance,
            .arc_segments_per_quadrant = request.arc_segments_per_quadrant,
            .preserve_collinear = request.options.preserve_collinear,
            .reverse_solution = request.options.reverse_solution,
            .coordinate_rounding = request.coordinate_rounding,
        },
        nullptr);
    return result;
}

auto assert_product_matches_scalar_reference(const next::offset_request64& request) -> void {
    const auto expected = execute_scalar_reference_offset(request);
    const auto actual = next::offset(request).closed;
    oracle::assert_paths_semantically_equal(expected, actual);
}

[[nodiscard]] auto pairwise_generated_requests() -> std::vector<next::offset_request64> {
    // Deterministic stratified sample: 100 cases cover every axis value and
    // rotate the axis strides so common pairings are exercised without running
    // the full 900-case Cartesian product in regular CI.
    constexpr std::array join_types{next::JoinType::Miter,
                                    next::JoinType::Bevel,
                                    next::JoinType::Square,
                                    next::JoinType::Round};
    constexpr std::array end_types{next::EndType::Polygon,
                                   next::EndType::Joined,
                                   next::EndType::Butt,
                                   next::EndType::Square,
                                   next::EndType::Round};
    constexpr std::array deltas{-1000.0, -2.0, 0.0, 2.0, 1000.0};
    constexpr std::array arc_tolerances{0.0, 0.25, 2.0};
    constexpr std::array miter_limits{1.0, 2.0, 10.0};

    std::vector<next::offset_request64> requests;
    requests.reserve(100U);
    for (std::size_t index = 0; index < 100U; ++index) {
        const auto end_type = end_types[(index * 2U + index / 5U) % end_types.size()];
        auto paths = end_type == next::EndType::Polygon
                         ? closed_subject(static_cast<std::int64_t>(index * 11U))
                         : open_subject(static_cast<std::int64_t>(index * 13U));
        requests.push_back(
            make_request(std::move(paths),
                         deltas[(index * 3U + index / 7U) % deltas.size()],
                         join_types[(index + index / 3U) % join_types.size()],
                         end_type,
                         arc_tolerances[(index + index / 11U) % arc_tolerances.size()],
                         miter_limits[(index * 2U + index / 13U) % miter_limits.size()]));
    }
    return requests;
}

}  // namespace

TEST(Clipper2NextOffsetBackendEquivalenceTests, PairwiseGeneratedRequestsMatchScalarReference) {
    const auto requests = pairwise_generated_requests();
    ASSERT_GE(requests.size(), 80U);
    ASSERT_LE(requests.size(), 120U);

    for (std::size_t index = 0; index < requests.size(); ++index) {
        SCOPED_TRACE(index);
        EXPECT_NO_THROW(assert_product_matches_scalar_reference(requests[index]));
    }
}
