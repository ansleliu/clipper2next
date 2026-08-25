#include "triangulation/private/triangulation_result_builder.h"

#include <algorithm>

namespace clipper2next::internal {
namespace {

[[nodiscard]] auto path_from_triangle(const triangulation_triangle& triangle) -> Path64 {
    Path64 result;
    result.reserve(3);
    result.push_back(triangle.edges[0]->vL->pt);
    result.push_back(triangle.edges[0]->vR->pt);
    const auto& edge = *triangle.edges[1];
    if (edge.vL->pt == result[0] || edge.vL->pt == result[1]) {
        result.push_back(edge.vR->pt);
    } else {
        result.push_back(edge.vL->pt);
    }
    return result;
}

}  // namespace

auto build_triangulation_result(const triangulation_context& context) -> Paths64 {
    Paths64 result;
    result.reserve(context.triangles.size());
    for (const auto triangle_ref : context.triangles) {
        const auto* triangle = triangle_ref.get();
        auto path = path_from_triangle(*triangle);
        const auto orientation = clipper2next::cross_product_sign(path[0], path[1], path[2]);
        if (orientation == 0) { continue; }
        if (orientation < 0) { std::reverse(path.begin(), path.end()); }
        result.push_back(path);
    }
    return result;
}

}  // namespace clipper2next::internal
