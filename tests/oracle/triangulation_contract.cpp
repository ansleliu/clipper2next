#include "../support/triangulation_contract.h"
#include "path_equivalence.h"
#include "clipper2/clipper.triangulation.h"
#include <ostream>

namespace clipper2next::tests::oracle {
namespace {
auto write_paths(std::ostream& out, const Paths64& paths) -> void {
    out << '[';
    for (std::size_t i = 0; i < paths.size(); ++i) {
        if (i) { out << ','; }
        out << '[';
        for (std::size_t j = 0; j < paths[i].size(); ++j) {
            if (j) { out << ','; }
            out << '[' << paths[i][j].x << ',' << paths[i][j].y << ']';
        }
        out << ']';
    }
    out << ']';
}
auto write_record(std::ostream& out,
                  const char* prefix,
                  std::size_t index,
                  bool delaunay,
                  const Paths64& input,
                  clipper_error_code error,
                  TriangulateResult status,
                  const Paths64& actual) -> void {
    out << prefix << " {\"version\":1,\"case_index\":" << index
        << ",\"use_delaunay\":" << (delaunay ? "true" : "false") << ",\"input\":";
    write_paths(out, input);
    out << ",\"outer_error\":" << static_cast<int>(error)
        << ",\"domain_status\":" << static_cast<int>(status) << ",\"triangles\":";
    write_paths(out, actual);
    out << "}\n";
}
}  // namespace
auto write_triangulation_contract_record(std::ostream& out,
                                         std::size_t index,
                                         bool delaunay,
                                         const Paths64& input,
                                         clipper_error_code error,
                                         TriangulateResult status,
                                         const Paths64& actual) -> void {
    write_record(out, "TRI_CONTRACT", index, delaunay, input, error, status, actual);
}
auto record_triangulation_case(std::ostream& out,
                               std::size_t index,
                               bool delaunay,
                               const Paths64& input) -> void {
    triangulation_request64 request;
    request.paths = input;
    request.use_delaunay = delaunay;
    const auto actual = triangulate_checked(request);
    write_triangulation_contract_record(
        out,
        index,
        delaunay,
        input,
        actual.has_value() ? clipper_error_code::ok : actual.error(),
        actual.has_value() ? actual.value().status : TriangulateResult::fail,
        actual.has_value() ? actual.value().triangles : Paths64{});
    Clipper2Lib::Paths64 legacy_triangles;
    const auto status =
        Clipper2Lib::Triangulate(to_legacy_paths(input), legacy_triangles, delaunay);
    TriangulateResult domain;
    switch (status) {
    case Clipper2Lib::TriangulateResult::success:
        domain = TriangulateResult::success;
        break;
    case Clipper2Lib::TriangulateResult::fail:
        domain = TriangulateResult::fail;
        break;
    case Clipper2Lib::TriangulateResult::no_polygons:
        domain = TriangulateResult::no_polygons;
        break;
    case Clipper2Lib::TriangulateResult::paths_intersect:
        domain = TriangulateResult::paths_intersect;
        break;
    default:
        throw std::runtime_error{"unknown legacy triangulation status"};
    }
    write_record(out,
                 "TRI_LEGACY",
                 index,
                 delaunay,
                 input,
                 clipper_error_code::ok,
                 domain,
                 to_next_paths(legacy_triangles));
}
}  // namespace clipper2next::tests::oracle
