#pragma once

#include "clipper2next/api/error.h"
#include "clipper2next/api/export.h"
#include "clipper2next/core/path.h"

namespace clipper2next {

enum class TriangulateResult { success, fail, no_polygons, paths_intersect };

struct triangulation_request64 {
    Paths64 paths{};
    bool use_delaunay{true};
};

struct triangulation_requestd {
    PathsD paths{};
    int decimal_precision{2};
    bool use_delaunay{true};
};

struct triangulation_result64 {
    TriangulateResult status{TriangulateResult::fail};
    Paths64 triangles;
};

struct triangulation_resultd {
    TriangulateResult status{TriangulateResult::fail};
    PathsD triangles;
};

[[nodiscard]] CLIPPER2NEXT_API auto triangulate(
    const triangulation_request64& request) -> triangulation_result64;
[[nodiscard]] CLIPPER2NEXT_API auto triangulate(
    const triangulation_requestd& request) -> triangulation_resultd;
[[nodiscard]] CLIPPER2NEXT_API auto triangulate_checked(
    const triangulation_request64& request)
    -> clipper_result<triangulation_result64>;
[[nodiscard]] CLIPPER2NEXT_API auto triangulate_checked(
    const triangulation_requestd& request)
    -> clipper_result<triangulation_resultd>;

CLIPPER2NEXT_API auto triangulate_into(
    const triangulation_request64& request, triangulation_result64& result)
    -> void;
CLIPPER2NEXT_API auto triangulate_into(
    const triangulation_requestd& request, triangulation_resultd& result) -> void;

}  // namespace clipper2next
