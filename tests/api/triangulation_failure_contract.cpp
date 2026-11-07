#include "clipper2next/triangulation.h"
#include "triangulation/private/triangulation_sweep_line.h"

#include <cstdio>

namespace {
unsigned sweep_calls{};
}

// Executable-local failure seam. The archive supplies the real boundary builder,
// executor and public facades, but does not pull in its normal sweep definition.
namespace clipper2next::internal {
auto run_triangulation_sweep(triangulation_context&, const triangulation_predicate_domain&) -> TriangulateResult {
    ++sweep_calls;
    return TriangulateResult::fail;
}
}  // namespace clipper2next::internal

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
    const auto empty = next::triangulate_checked(Request{});
    return !checked && checked.error() == next::clipper_error_code::internal_error && throwing &&
           empty && empty->status == next::TriangulateResult::no_polygons &&
           empty->triangles.empty();
}

int main() {
    const next::triangulation_request64 integer{{
        {{0,0},{10,0},{10,10},{0,10}}, {{0,5},{2,7},{2,3}}}};
    const next::triangulation_requestd floating{{
        {{0,0},{10,0},{10,10},{0,10}}, {{0,5},{2,7},{2,3}}}};
    const auto integer_ok = check(integer);
    const auto floating_ok = check(floating);
    std::printf("triangulation forced sweep fail: calls=%u integer=%d double=%d\n",
                sweep_calls,
                integer_ok,
                floating_ok);
    return integer_ok && floating_ok && sweep_calls == 4U ? 0 : 1;
}
