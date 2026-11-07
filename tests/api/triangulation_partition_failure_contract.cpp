#include "clipper2next/triangulation.h"
#include "triangulation/private/triangulation_partition.h"
#include <cstdio>

namespace {
unsigned cdt_calls{};
unsigned sweep_calls{};
}

// Both entry points share the partition object in the production archive.
namespace clipper2next::internal {
auto try_build_partitioned_triangulation_graph(
    triangulation_context&, const triangulation_predicate_domain&) -> bool {
    ++cdt_calls;
    raise_clipper_error(clipper_error_code::internal_error);
}

auto try_prepare_partitioned_sweep(
    triangulation_context&, const triangulation_predicate_domain&) -> bool {
    ++sweep_calls;
    raise_clipper_error(clipper_error_code::internal_error);
}
}

namespace next = clipper2next;

template <typename Request>
auto check(const Request& request) -> bool {
    const auto checked = next::triangulate_checked(request);
    bool throwing = false;
    try {
        (void)next::triangulate(request);
    } catch (const next::clipper_error& error) {
        throwing = error.code() == next::clipper_error_code::internal_error;
    }
    Request empty_request;
    empty_request.use_delaunay = request.use_delaunay;
    const auto empty = next::triangulate_checked(empty_request);
    return !checked && checked.error() == next::clipper_error_code::internal_error &&
           throwing && empty && empty->status == next::TriangulateResult::no_polygons &&
           empty->triangles.empty();
}

template <typename Request>
auto check_both(Request request) -> bool {
    request.use_delaunay = true;
    const auto cdt = check(request);
    request.use_delaunay = false;
    const auto sweep = check(request);
    return cdt && sweep;
}

int main() {
    const next::triangulation_request64 integer{{
        {{0,0},{10,0},{10,10},{0,10}}, {{3,3},{3,7},{7,7},{7,3}}}};
    const next::triangulation_requestd floating{{
        {{0,0},{10,0},{10,10},{0,10}}, {{3,3},{3,7},{7,7},{7,3}}}};
    const auto integer_ok = check_both(integer);
    const auto floating_ok = check_both(floating);
    std::printf("triangulation forced partition fail: cdt=%u sweep=%u integer=%d double=%d\n",
                cdt_calls, sweep_calls, integer_ok, floating_ok);
    return integer_ok && floating_ok && cdt_calls == 4U && sweep_calls == 4U ? 0 : 1;
}
