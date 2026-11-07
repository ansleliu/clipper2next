#pragma once
#include "clipper2next/core.h"
#include "clipper2next/triangulation.h"
#include <iosfwd>
namespace clipper2next::tests::oracle {
auto write_triangulation_contract_record(std::ostream& output,
                                         std::size_t case_index,
                                         bool use_delaunay,
                                         const Paths64& input,
                                         clipper_error_code outer_error,
                                         TriangulateResult domain_status,
                                         const Paths64& triangles) -> void;
auto record_triangulation_case(std::ostream& output,
                               std::size_t case_index,
                               bool use_delaunay,
                               const Paths64& input) -> void;
}  // namespace clipper2next::tests::oracle
#include <istream>

namespace clipper2next::test_support {
inline auto read_exact_point(std::istream& input, Point64& point) -> bool {
    return static_cast<bool>(input >> point.x >> point.y) && point.x >= -MAX_COORD &&
           point.x <= MAX_COORD && point.y >= -MAX_COORD && point.y <= MAX_COORD;
}
}  // namespace clipper2next::test_support
