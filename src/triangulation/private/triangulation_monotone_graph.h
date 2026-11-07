#pragma once

#include "triangulation/private/triangulation_context.h"
#include "triangulation/private/triangulation_monotone.h"
#include "triangulation/private/triangulation_topology.h"

namespace clipper2next::internal {

class monotone_graph_builder final {
    triangulation_context& context_;
    std::span<const triangulation_vertex_ref> vertices_;
    const triangulation_predicate_domain& domain_;

    auto edge(std::size_t first, std::size_t second) -> triangulation_edge* {
        auto* a = vertices_[first].get();
        auto* b = vertices_[second].get();
        const auto& adjacency = a->edges.size() <= b->edges.size() ? a->edges : b->edges;
        for (auto item : adjacency) {
            if ((item->first == a && item->second == b) ||
                (item->first == b && item->second == a)) { return item.get(); }
        }
        auto* result = create_triangulation_edge(context_, a, b);
        enqueue_pending_delaunay(context_, result);
        return result;
    }

public:
    monotone_graph_builder(triangulation_context& context,
                           std::span<const triangulation_vertex_ref> vertices,
                           const triangulation_predicate_domain& domain)
        : context_(context), vertices_(vertices), domain_(domain) {}

    auto orientation(std::size_t a, std::size_t b, std::size_t c) const noexcept -> int {
        return domain_.orientation(vertices_[a]->pt, vertices_[b]->pt, vertices_[c]->pt);
    }

    auto face(std::size_t a, std::size_t b, std::size_t c) -> void {
        if (!orientation(a, b, c)) {
            raise_clipper_error(clipper_error_code::internal_error);
        }
        auto* first = edge(a, b);
        auto* second = edge(b, c);
        auto* third = edge(c, a);
        auto* triangle = create_triangulation_triangle(context_, first, second, third);
        for (auto side : triangle->edges) {
            if (side->triB) { raise_clipper_error(clipper_error_code::internal_error); }
            if (side->triA) { side->triB = triangle; }
            else { side->triA = triangle; }
        }
    }
};

template <class Builder>
inline auto walk_monotone_faces(monotone_workspace& workspace,
                                std::span<const monotone_vertex> order,
                                Builder& builder) -> void {
    auto& stack = workspace.stack;
    stack.clear();
    stack.reserve(order.size());
    stack.push_back(order[0]);
    stack.push_back(order[1]);
    for (std::size_t i = 2; i + 1 < order.size(); ++i) {
        const auto current = order[i];
        if (current.chain != stack.back().chain) {
            while (stack.size() > 1) {
                const auto previous = stack.back();
                stack.pop_back();
                builder.face(current.index, previous.index, stack.back().index);
            }
            stack.clear();
            stack.push_back(order[i - 1]);
            stack.push_back(current);
        } else {
            auto previous = stack.back();
            stack.pop_back();
            while (!stack.empty()) {
                const auto turn = builder.orientation(
                    current.index, previous.index, stack.back().index);
                if (!turn || ((turn < 0) != (current.chain == 1))) { break; }
                builder.face(current.index, previous.index, stack.back().index);
                previous = stack.back();
                stack.pop_back();
            }
            stack.push_back(previous);
            stack.push_back(current);
        }
    }
    while (stack.size() > 1) {
        const auto previous = stack.back();
        stack.pop_back();
        builder.face(order.back().index, previous.index, stack.back().index);
    }
}

inline auto build_monotone_faces(triangulation_context& context,
                                 std::span<const triangulation_vertex_ref> vertices,
                                 std::span<const monotone_vertex> order,
                                 const triangulation_predicate_domain& domain) -> void {
    monotone_graph_builder builder{context, vertices, domain};
    walk_monotone_faces(context.monotone, order, builder);
}

}  // namespace clipper2next::internal
