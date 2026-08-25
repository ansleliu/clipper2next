#include "triangulation/private/triangulation_path_builder.h"

#include "support/private/checked_size.h"
#include "triangulation/private/triangulation_legalizer.h"

namespace clipper2next::internal {
namespace {

[[nodiscard]] auto find_local_minimum_index(const Path64& path,
                                            std::size_t length,
                                            std::size_t& index) -> bool {
    if (length < 3) { return false; }

    const auto start_index = index;
    auto next_index = (index + 1) % length;
    while (path[next_index].y <= path[index].y) {
        index = next_index;
        next_index = (next_index + 1) % length;
        if (index == start_index) { return false; }
    }

    while (path[next_index].y >= path[index].y) {
        index = next_index;
        next_index = (next_index + 1) % length;
    }
    return true;
}

[[nodiscard]] auto previous_index(std::size_t index, std::size_t length) -> std::size_t {
    return index == 0 ? length - 1 : index - 1;
}

[[nodiscard]] auto next_index(std::size_t index, std::size_t length) -> std::size_t {
    return (index + 1) % length;
}

[[nodiscard]] auto distance_squared(const Point64& first, const Point64& second) -> double {
    return square(first.x - second.x) + square(first.y - second.y);
}

}  // namespace

auto add_triangulation_path(triangulation_context& context, const Path64& path) -> void {
    auto length = path.size();
    std::size_t first_minimum = 0;
    if (!find_local_minimum_index(path, length, first_minimum)) { return; }

    auto previous = previous_index(first_minimum, length);
    while (path[previous] == path[first_minimum]) { previous = previous_index(previous, length); }
    auto next = next_index(first_minimum, length);

    auto index = first_minimum;
    while (cross_product_sign(path[previous], path[index], path[next]) == 0) {
        const auto found_next_minimum = find_local_minimum_index(path, length, index);
        (void)found_next_minimum;
        if (index == first_minimum) { return; }
        previous = previous_index(index, length);
        while (path[previous] == path[index]) { previous = previous_index(previous, length); }
        next = next_index(index, length);
    }

    const auto first_vertex_offset = context.vertices.size();
    auto* first_vertex = create_triangulation_vertex(context, path[index]);
    if (LeftTurning(path[previous], path[index], path[next])) { first_vertex->innerLM = true; }

    auto* previous_vertex = first_vertex;
    index = next;
    for (;;) {
        context.local_minima.push_back(previous_vertex);
        if (!context.lowermost_vertex || previous_vertex->pt.y > context.lowermost_vertex->pt.y ||
            (previous_vertex->pt.y == context.lowermost_vertex->pt.y &&
             previous_vertex->pt.x < context.lowermost_vertex->pt.x)) {
            context.lowermost_vertex = previous_vertex;
        }

        next = next_index(index, length);
        if (cross_product_sign(previous_vertex->pt, path[index], path[next]) == 0) {
            index = next;
            continue;
        }

        while (path[index].y <= previous_vertex->pt.y) {
            auto* vertex = create_triangulation_vertex(context, path[index]);
            (void)create_triangulation_edge(
                context, previous_vertex, vertex, triangulation_edge_kind::ascend);
            previous_vertex = vertex;
            index = next;
            next = next_index(index, length);

            while (cross_product_sign(previous_vertex->pt, path[index], path[next]) == 0) {
                index = next;
                next = next_index(index, length);
            }
        }

        auto* previous_previous_vertex = previous_vertex;
        while (index != first_minimum && path[index].y >= previous_vertex->pt.y) {
            auto* vertex = create_triangulation_vertex(context, path[index]);
            (void)create_triangulation_edge(
                context, vertex, previous_vertex, triangulation_edge_kind::descend);
            previous_previous_vertex = previous_vertex;
            previous_vertex = vertex;
            index = next;
            next = next_index(index, length);

            while (cross_product_sign(previous_vertex->pt, path[index], path[next]) == 0) {
                index = next;
                next = next_index(index, length);
            }
        }

        if (index == first_minimum) { break; }
        if (LeftTurning(previous_previous_vertex->pt, previous_vertex->pt, path[index])) {
            previous_vertex->innerLM = true;
        }
    }

    (void)create_triangulation_edge(
        context, first_vertex, previous_vertex, triangulation_edge_kind::descend);

    length = context.vertices.size() - first_vertex_offset;
    if (length < 3 ||
        (length == 3 && ((distance_squared(context.vertices[first_vertex_offset]->pt,
                                           context.vertices[first_vertex_offset + 1]->pt) <= 1) ||
                         (distance_squared(context.vertices[first_vertex_offset + 1]->pt,
                                           context.vertices[first_vertex_offset + 2]->pt) <= 1) ||
                         (distance_squared(context.vertices[first_vertex_offset + 2]->pt,
                                           context.vertices[first_vertex_offset]->pt) <= 1)))) {
        for (auto vertex_index = first_vertex_offset; vertex_index < context.vertices.size();
             ++vertex_index) {
            context.vertices[vertex_index]->edges.clear();
        }
    }
}

auto add_triangulation_paths(triangulation_context& context, const Paths64& paths) -> bool {
    std::size_t total_vertex_count = 0;
    for (const auto& path : paths) {
        total_vertex_count = checked_size_add(total_vertex_count, path.size());
    }
    if (total_vertex_count == 0) { return false; }

    const auto required_vertices = checked_size_add(context.vertices.size(), total_vertex_count);
    if (context.vertices.capacity() < required_vertices) {
        context.vertices.reserve(required_vertices);
    }

    const auto required_edges = checked_size_add(context.edges.size(), total_vertex_count);
    if (context.edges.capacity() < required_edges) { context.edges.reserve(required_edges); }

    const auto required_minima = checked_size_add(context.local_minima.size(), total_vertex_count);
    if (context.local_minima.capacity() < required_minima) {
        context.local_minima.reserve(required_minima);
    }

    for (const auto& path : paths) { add_triangulation_path(context, path); }
    return context.vertices.size() > 2;
}

}  // namespace clipper2next::internal
