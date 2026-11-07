#include "triangulation/private/triangulation_partition.h"
#include "triangulation/private/triangulation_partition_projector.h"
#include "triangulation/private/triangulation_partition_status.h"
#include "triangulation/private/triangulation_monotone_graph.h"
#include "triangulation/private/triangulation_boundary_events.h"
#include "support/private/checked_size.h"
#include "geometry/private/integer_area_sign.h"
#include <algorithm>
#include <cassert>
#include <numeric>
#include <utility>

namespace clipper2next::internal {
namespace {
[[noreturn]] auto invalid() -> void {
    raise_clipper_error(clipper_error_code::internal_error);
}

auto admitted(const triangulation_context& context) -> bool {
    if (context.vertices.empty() || context.edges.size() != context.vertices.size() ||
        context.boundary_events.size() != context.vertices.size()) { return false; }
    // Successful boundary preparation covers every closed-contour vertex with
    // at least two incidences and assigns every edge endpoint exactly once.
    // Sum(degree)=2E and E=V therefore prove degree two and one event per vertex.
    // Keep producer-contract assertions without a second release-time scan.
#ifndef NDEBUG
    for (auto vertex : context.vertices) {
        assert(vertex->edges.size() == 2 && vertex->event_end == vertex->event_begin + 1);
    }
    for (const auto& event : context.boundary_events) {
        assert(event.incoming->second == event.vertex && event.outgoing->first == event.vertex &&
               event.incoming->kind != triangulation_edge_kind::loose &&
               event.outgoing->kind != triangulation_edge_kind::loose);
    }
#endif
    return true;
}

auto merge_event(const boundary_corner_event& event) noexcept -> bool {
    return event.kind == boundary_event_kind::close && event.reflex;
}

auto add_partition_diagonal(triangulation_context& context, std::size_t a, std::size_t b)
    -> void {
    if (a == b) { invalid(); }
    const auto& first = context.boundary_events[a];
    const auto& second = context.boundary_events[b];
    if (!first.sector.contains_ray(ray_to(first.vertex->pt, second.vertex->pt), false) ||
        !second.sector.contains_ray(ray_to(second.vertex->pt, first.vertex->pt), false)) {
        invalid();
    }
    auto* edge = create_triangulation_edge(context, first.vertex.get(), second.vertex.get());
    edge->first_event = a;
    edge->second_event = b;
    enqueue_pending_delaunay(context, edge);
}

auto partition_constraints(triangulation_context& context,
                           const triangulation_predicate_domain& domain) -> void {
    const auto count = context.boundary_events.size();
    std::vector<triangulation_edge_ref> sources;
    sources.reserve(count);
    for (const auto& event : context.boundary_events) { sources.push_back(event.outgoing); }
    triangulation_partition_status status{sources, domain};
    std::vector<std::size_t> order(count);
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](auto a, auto b) {
        return sweep_before(context.boundary_events[a].vertex->pt,
                            context.boundary_events[b].vertex->pt);
    });
    const auto consume_merge = [&](std::size_t edge, std::size_t vertex) {
        const auto helper = status.helper(edge);
        if (merge_event(context.boundary_events[helper])) {
            add_partition_diagonal(context, vertex, helper);
        }
    };
    const auto left_edge = [&](Point64 point) {
        const auto edge = status.left(point);
        if (edge == no_boundary_event) { invalid(); }
        return edge;
    };
    for (const auto id : order) {
        const auto& event = context.boundary_events[id];
        const auto incoming = event.incoming->first_event;
        if (event.kind == boundary_event_kind::open) {
            if (event.reflex) {
                const auto left = left_edge(event.vertex->pt);
                add_partition_diagonal(context, id, status.helper(left));
                status.set_helper(left, id);
            }
            status.insert(id, id);
        } else if (event.kind == boundary_event_kind::close) {
            consume_merge(incoming, id);
            status.erase(incoming);
            if (event.reflex) {
                const auto left = left_edge(event.vertex->pt);
                consume_merge(left, id);
                status.set_helper(left, id);
            }
        } else if (sweep_before(event.vertex->pt, event.outgoing->second->pt)) {
            consume_merge(incoming, id);
            status.erase(incoming);
            status.insert(id, id);
        } else {
            const auto left = left_edge(event.vertex->pt);
            consume_merge(left, id);
            status.set_helper(left, id);
        }
    }
    if (!status.empty()) { invalid(); }
}

auto origin(const triangulation_context& context, std::size_t arc) noexcept -> triangulation_vertex_ref {
    const auto edge = context.edges[arc / 2U];
    return (arc & 1U) == 0U ? edge->first : edge->second;
}
auto destination(const triangulation_context& context, std::size_t arc) noexcept -> triangulation_vertex_ref {
    const auto edge = context.edges[arc / 2U];
    return (arc & 1U) == 0U ? edge->second : edge->first;
}

auto origin_event(const triangulation_context& context, std::size_t arc) noexcept -> std::size_t {
    const auto edge = context.edges[arc / 2U];
    return (arc & 1U) == 0U ? edge->first_event : edge->second_event;
}
auto occupied(const triangulation_context& context, std::size_t arc) noexcept -> bool {
    return (arc & 1U) == 0U || is_loose_edge(*context.edges[arc / 2U]);
}

auto face_successors(const triangulation_context& context,
                     std::size_t arc_count,
                     const triangulation_predicate_domain& domain) -> std::vector<std::size_t> {
    const auto count = context.boundary_events.size();
    std::vector<std::size_t> offsets(checked_size_add(count, 1U), 0);
    for (std::size_t arc = 0; arc != arc_count; ++arc) {
        ++offsets[origin_event(context, arc) + 1U];
    }
    std::partial_sum(offsets.begin(), offsets.end(), offsets.begin());
    std::vector<std::size_t> outgoing(arc_count);
    for (std::size_t id = 0; id != arc_count; ++id) {
        outgoing[offsets[origin_event(context, id)]++] = id;
    }
    // Placement consumes each start offset into that vertex's end offset.
    std::vector<std::size_t> result(arc_count, no_boundary_event);
    for (std::size_t vertex = 0; vertex != count; ++vertex) {
        const auto begin = vertex ? offsets[vertex - 1U] : 0U;
        auto rays = std::span<std::size_t>{outgoing}.subspan(
            begin, offsets[vertex] - begin);
        std::sort(rays.begin(), rays.end(), [&](auto a, auto b) {
            if (a == b) { return false; }
            const auto point = origin(context, a)->pt;
            const auto first = ray_to(point, destination(context, a)->pt);
            const auto second = ray_to(point, destination(context, b)->pt);
            const auto first_upper = boundary_ray_upper(first);
            const auto second_upper = boundary_ray_upper(second);
            if (first_upper != second_upper) { return first_upper; }
            const auto turn = domain.orientation(
                point, destination(context, a)->pt, destination(context, b)->pt);
            if (!turn) { invalid(); }
            return turn > 0;
        });
        for (std::size_t i = 0; i != rays.size(); ++i) {
            result[rays[i] ^ 1U] = rays[i ? i - 1 : rays.size() - 1];
        }
    }
    return result;
}

template <bool Project>
auto materialize_partition_faces(triangulation_context& context,
                                 const triangulation_predicate_domain& domain) -> void {
    // New face diagonals only append edges. Original ordinals and endpoints
    // remain fixed, so arc identity needs no second edge directory.
    const auto arc_count = checked_size_multiply(context.edges.size(), 2U);
    auto next = face_successors(context, arc_count, domain);
    auto& vertices = context.partition_vertices;
    auto& path = context.partition_path;
    for (std::size_t seed = 0; seed != arc_count; ++seed) {
        if (!occupied(context, seed) || next[seed] == no_boundary_event) { continue; }
        vertices.clear();
        path.clear();
        if constexpr (Project) { context.partition_sides.clear(); }
        auto current = seed;
        do {
            if (current >= arc_count || next[current] == no_boundary_event || !occupied(context, current)) {
                invalid();
            }
            const auto successor = std::exchange(next[current], no_boundary_event);
            const auto vertex = origin(context, current);
            vertices.push_back(vertex);
            path.push_back(vertex->pt);
            if constexpr (Project) { context.partition_sides.push_back(context.edges[current / 2U]); }
            current = successor;
        } while (current != seed);
        const auto layout = prepare_monotone_layout(path, domain, context.monotone);
        if (!layout || layout->direction != 1) { invalid(); }
        if constexpr (Project) {
            // Each occupied arc is consumed exactly once below. A certified
            // positive monotone cycle supplies all of its nondegenerate faces.
            const auto before = context.partition_faces.size();
            partition_face_projector projector{context, domain};
            walk_monotone_faces(context.monotone, layout->order, projector);
            if (context.partition_faces.size() - before != vertices.size() - 2U) { invalid(); }
        } else {
            build_monotone_faces(context, vertices, layout->order, domain);
        }
    }
    for (std::size_t id = 0; id != arc_count; ++id) {
        if (occupied(context, id) != (next[id] == no_boundary_event)) { invalid(); }
    }
}
}  // namespace

auto try_build_partitioned_triangulation_graph(
    triangulation_context& context, const triangulation_predicate_domain& domain) -> bool {
    if (!admitted(context)) { return false; }
    partition_constraints(context, domain);
    materialize_partition_faces<false>(context, domain);
    return true;
}

auto try_prepare_partitioned_sweep(triangulation_context& context,
                                   const triangulation_predicate_domain& domain) -> bool {
    assert(!context.use_delaunay);
    if (!admitted(context)) { return false; }
    partition_constraints(context, domain);
    // Degree-two admission gives disjoint directed rings. Each normalized
    // shell subtracts two from V and each hole adds two (Euler's formula).
    auto face_count = context.source_paths.point_count();
    for (std::size_t i = 0; i < context.source_paths.size(); ++i) {
        if (signed_area_sign(context.source_paths[i]) < 0) {
            face_count = checked_size_add(face_count, 2U);
        } else {
            face_count -= 2U;
        }
    }
    context.partition_faces.reserve(face_count);
    materialize_partition_faces<true>(context, domain);
    if (context.partition_faces.size() != face_count) { invalid(); }
    return true;
}
}  // namespace clipper2next::internal
