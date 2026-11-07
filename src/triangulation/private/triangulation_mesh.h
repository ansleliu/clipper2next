#pragma once

#include "triangulation/private/triangulation_types.h"
#include <array>
#include <span>

namespace clipper2next::internal {
struct triangulation_context;
class triangulation_predicate_domain;
class triangulation_mesh_view final {
    std::span<const triangulation_triangle_ref> triangles_;
    explicit triangulation_mesh_view(std::span<const triangulation_triangle_ref> triangles) noexcept
        : triangles_(triangles) {}
    friend auto validate_triangulation_mesh(triangulation_context&,
                                            const triangulation_predicate_domain&) -> triangulation_mesh_view;
public:
    triangulation_mesh_view() noexcept = default;
    [[nodiscard]] auto size() const noexcept -> std::size_t { return triangles_.size(); }
    [[nodiscard]] auto triangle(std::size_t index) const noexcept -> const triangulation_triangle& {
        return *triangles_[index];
    }
};

[[nodiscard]] inline auto triangle_vertices(const triangulation_triangle& triangle) noexcept
    -> std::array<triangulation_vertex_ref, 3> {
    const auto a = triangle.edges[0]->vL, b = triangle.edges[0]->vR;
    const auto& edge = *triangle.edges[1];
    const auto c = edge.vL == a || edge.vL == b ? edge.vR : edge.vL;
    return {a, b, c};
}
[[nodiscard]] auto validate_triangulation_mesh(triangulation_context& context,
                                               const triangulation_predicate_domain& domain)
    -> triangulation_mesh_view;
}  // namespace clipper2next::internal
