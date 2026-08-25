#pragma once

#include "triangulation/private/triangulation_types.h"

#include <array>
#include <cstddef>
#include <vector>

namespace clipper2next::internal {

struct vertex_id {
    std::size_t value = 0;
    friend auto operator==(vertex_id, vertex_id) -> bool = default;
};

struct edge_id {
    std::size_t value = 0;
    friend auto operator==(edge_id, edge_id) -> bool = default;
};

struct triangle_id {
    std::size_t value = 0;
    friend auto operator==(triangle_id, triangle_id) -> bool = default;
};

struct triangulation_graph_vertex {
    Point64 point;
};

struct triangulation_graph_edge {
    vertex_id first;
    vertex_id second;
    triangulation_edge_kind kind = triangulation_edge_kind::loose;
};

struct triangulation_graph_triangle {
    std::array<edge_id, 3> edges;
};

class triangulation_graph final {
public:
    [[nodiscard]] auto add_vertex(Point64 point) -> vertex_id;
    [[nodiscard]] auto add_edge(vertex_id first,
                                vertex_id second,
                                triangulation_edge_kind kind = triangulation_edge_kind::loose)
        -> edge_id;
    [[nodiscard]] auto add_triangle(edge_id first, edge_id second, edge_id third) -> triangle_id;

    [[nodiscard]] auto vertex(vertex_id id) const -> const triangulation_graph_vertex&;
    [[nodiscard]] auto edge(edge_id id) const -> const triangulation_graph_edge&;
    [[nodiscard]] auto triangle(triangle_id id) const -> const triangulation_graph_triangle&;

    [[nodiscard]] auto vertex_count() const noexcept -> std::size_t;
    [[nodiscard]] auto edge_count() const noexcept -> std::size_t;
    [[nodiscard]] auto triangle_count() const noexcept -> std::size_t;

    auto clear() noexcept -> void;

private:
    std::vector<triangulation_graph_vertex> vertices_;
    std::vector<triangulation_graph_edge> edges_;
    std::vector<triangulation_graph_triangle> triangles_;
};

}  // namespace clipper2next::internal
