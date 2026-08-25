#pragma once

#include "clipper2next/api/options.h"
#include "clipper2next/api/result.h"
#include "clipper2next/offset/types.h"

namespace clipper2next {

struct offset_request64 final {
    Paths64 paths{};
    double delta{0.0};
    JoinType join_type{JoinType::Miter};
    EndType end_type{EndType::Polygon};
    double miter_limit{2.0};
    // Maximum radial approximation error in coordinate units. Zero selects
    // Clipper2's delta-relative default tolerance.
    double arc_tolerance{0.0};
    // Nonzero selects an exact number of round-join segments per quadrant
    // and takes precedence over arc_tolerance.
    std::size_t arc_segments_per_quadrant{};
    geotypes::CoordinateRounding coordinate_rounding{
        geotypes::CoordinateRounding::NearestEven};
    execution_options options{};
};

}  // namespace clipper2next
