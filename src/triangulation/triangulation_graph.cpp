#include "triangulation/private/triangulation_graph.h"

namespace clipper2next::internal {

auto triangulation_graph::add_vertex(Point64 point) -> vertex_id {
    const auto id = vertex_id{vertices_.size()};
    vertices_.push_back({point});
    return id;
}

auto triangulation_graph::add_edge(vertex_id first, vertex_id second, triangulation_edge_kind kind)
    -> edge_id {
    const auto id = edge_id{edges_.size()};
    edges_.push_back({first, second, kind});
    return id;
}

auto triangulation_graph::add_triangle(edge_id first, edge_id second, edge_id third)
    -> triangle_id {
    const auto id = triangle_id{triangles_.size()};
    triangles_.push_back({{first, second, third}});
    return id;
}

auto triangulation_graph::vertex(vertex_id id) const -> const triangulation_graph_vertex& {
    return vertices_.at(id.value);
}

auto triangulation_graph::edge(edge_id id) const -> const triangulation_graph_edge& {
    return edges_.at(id.value);
}

auto triangulation_graph::triangle(triangle_id id) const -> const triangulation_graph_triangle& {
    return triangles_.at(id.value);
}

auto triangulation_graph::vertex_count() const noexcept -> std::size_t {
    return vertices_.size();
}

auto triangulation_graph::edge_count() const noexcept -> std::size_t {
    return edges_.size();
}

auto triangulation_graph::triangle_count() const noexcept -> std::size_t {
    return triangles_.size();
}

auto triangulation_graph::clear() noexcept -> void {
    vertices_.clear();
    edges_.clear();
    triangles_.clear();
}

}  // namespace clipper2next::internal
