#include "triangulation/private/triangulation_executor.h"

#include "triangulation/private/triangulation_boundary.h"
#include "triangulation/private/triangulation_context.h"
#include "triangulation/private/triangulation_delaunay.h"
#include "triangulation/private/triangulation_result_builder.h"
#include "triangulation/private/triangulation_sweep_line.h"

namespace clipper2next::internal {
namespace {

struct reusable_triangulation_context_slot final {
    triangulation_context context{};
};

[[nodiscard]] auto reusable_triangulation_context_storage()
    -> reusable_triangulation_context_slot& {
    thread_local reusable_triangulation_context_slot slot;
    return slot;
}

[[nodiscard]] auto acquire_triangulation_context() -> triangulation_context& {
    auto& context = reusable_triangulation_context_storage().context;
    context.clear();
    return context;
}

}  // namespace

auto execute_triangulation(const Paths64& paths,
                           bool use_delaunay,
                           TriangulateResult& result) -> Paths64 {
    auto& context = acquire_triangulation_context();
    context.use_delaunay = use_delaunay;

    if (!build_triangulation_boundary(context, paths)) {
        result = TriangulateResult::no_polygons;
        return {};
    }

    const auto sweep_result = run_triangulation_sweep(context);
    if (sweep_result != TriangulateResult::success) {
        result = sweep_result;
        return {};
    }

    if (!legalize_pending_delaunay_edges(context)) {
        result = TriangulateResult::fail;
        return {};
    }
    auto triangles = build_triangulation_result(context);
    result = TriangulateResult::success;
    return triangles;
}

auto release_triangulation_execution_thread_state() noexcept -> void {
    reusable_triangulation_context_storage().context.release();
}

}  // namespace clipper2next::internal
