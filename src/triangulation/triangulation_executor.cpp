#include "triangulation/private/triangulation_executor.h"
#include "triangulation/private/triangulation_operation.h"

#include "triangulation/private/triangulation_boundary.h"
#include "triangulation/private/triangulation_context.h"
#include "triangulation/private/triangulation_simple.h"
#include "triangulation/private/triangulation_partition.h"
#include "triangulation/private/triangulation_delaunay.h"
#include "triangulation/private/triangulation_result_builder.h"
#include "triangulation/private/triangulation_sweep_line.h"
#include "support/private/paths_rebuild.h"
#include <optional>
#include <type_traits>

namespace clipper2next::internal {
namespace {

auto graph_succeeded(triangulation_context& context, TriangulateResult result) -> bool {
    if (result == TriangulateResult::fail) { raise_clipper_error(clipper_error_code::internal_error); }
    if (result == TriangulateResult::success) { return true; }
    context.triangles.clear();
    return false;
}

auto prepare_general_boundary(triangulation_context& context, const Paths64& paths,
                              TriangulateResult& result) -> std::optional<triangulation_predicate_domain> {
    result = build_triangulation_boundary(context, paths);
    if (!graph_succeeded(context, result)) { return std::nullopt; }
    return prepare_triangulation_predicate_domain(context);
}

auto run_general_sweep(triangulation_context& context,
                       const triangulation_predicate_domain& domain,
                       TriangulateResult& result) -> bool {
    result = run_triangulation_sweep(context, domain);
    return graph_succeeded(context, result);
}

auto build_general_graph(triangulation_context& context, const Paths64& paths,
                         TriangulateResult& result) -> std::optional<triangulation_predicate_domain> {
    const auto domain = prepare_general_boundary(context, paths, result);
    if (!domain || (!try_build_partitioned_triangulation_graph(context, *domain) &&
                    !run_general_sweep(context, *domain, result))) { return std::nullopt; }
    return domain;
}

auto finish_graph(triangulation_context& context, const triangulation_predicate_domain& domain,
                   TriangulateResult& result) -> triangulation_mesh_view {
    if (!legalize_pending_delaunay_edges(context, domain)) {
        raise_clipper_error(clipper_error_code::internal_error);
    }
    const auto mesh = validate_triangulation_mesh(context, domain);
    result = TriangulateResult::success;
    return mesh;
}

template <class C>
auto execute_sweep_into(triangulation_operation_lease& operation, const Paths64& paths,
                         TriangulateResult& result, Paths<C>& output, double scale) -> void {
    auto& context = operation.context();
    context.use_delaunay = false;
    bool completed;
    if constexpr (std::is_same_v<C, int64_t>) {
        completed = try_build_simple_triangulation_result(context, paths, output);
    } else {
        completed = try_build_simple_triangulation_result(context, paths, output, scale);
    }
    if (completed) {
        result = TriangulateResult::success;
        return;
    }
    const auto domain = prepare_general_boundary(context, paths, result);
    if (domain && try_prepare_partitioned_sweep(context, *domain)) {
        const auto& faces = context.partition_faces;
        paths_rebuild<C> transaction{output};
        transaction.prepare(faces.size(), [](auto) { return 3U; });
        transaction.begin();
        for (std::size_t i = 0; i < faces.size(); ++i) {
            for (auto vertex : faces[i]) {
                const auto& point = vertex->pt;
                if constexpr (std::is_same_v<C, int64_t>) { output[i].push_back(point); }
                else { output[i].emplace_back(point.x * scale, point.y * scale); }
            }
        }
        transaction.finish();
        result = TriangulateResult::success;
        return;
    }
    const auto mesh = domain && run_general_sweep(context, *domain, result)
        ? finish_graph(context, *domain, result) : triangulation_mesh_view{};
    if constexpr (std::is_same_v<C, int64_t>) { build_triangulation_result_into(mesh, output); }
    else { build_triangulation_result_into(mesh, output, scale); }
}

}  // namespace

auto execute_triangulation_graph(triangulation_operation_lease& operation, const Paths64& paths,
                                 bool use_delaunay, TriangulateResult& result)
    -> triangulation_mesh_view {
    auto& context = operation.context();
    context.use_delaunay = use_delaunay;
    auto domain = try_build_simple_triangulation_graph(context, paths);
    if (!domain) { domain = build_general_graph(context, paths, result); }
    return domain ? finish_graph(context, *domain, result) : triangulation_mesh_view{};
}

auto execute_sweep_triangulation_into(triangulation_operation_lease& operation, const Paths64& paths,
                                      TriangulateResult& result, Paths64& output) -> void {
    execute_sweep_into(operation, paths, result, output, 1.0);
}

auto execute_sweep_triangulation_into(triangulation_operation_lease& operation, const Paths64& paths,
                                      TriangulateResult& result, PathsD& output, double scale) -> void {
    execute_sweep_into(operation, paths, result, output, scale);
}

auto execute_triangulation(const Paths64& paths, bool delaunay, TriangulateResult& status)
    -> Paths64 {
    auto operation = triangulation_operation_lease{};
    if (!delaunay) {
        Paths64 output;
        execute_sweep_triangulation_into(operation, paths, status, output);
        return output;
    }
    return build_triangulation_result(execute_triangulation_graph(operation, paths, delaunay, status));
}

}  // namespace clipper2next::internal
