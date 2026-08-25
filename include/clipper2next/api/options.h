#pragma once

#include "clipper2next/geometry/line_intersections.h"

namespace clipper2next {

struct execution_options final {
    bool preserve_collinear{false};
    bool reverse_solution{false};
    predicate_policy intersection_policy{};
};

}  // namespace clipper2next
