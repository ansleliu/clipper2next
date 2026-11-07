#include "triangulation/private/triangulation_boundary_events.h"
#include <algorithm>
#include <limits>

namespace clipper2next::internal {
namespace {
static_assert(std::numeric_limits<std::size_t>::digits <= std::numeric_limits<uint64_t>::digits);

auto sign(int64_t value) noexcept -> int {
    return (value > 0) - (value < 0);
}
auto lexsign(int base, int infinitesimal) noexcept -> int {
    return base ? base : infinitesimal;
}
auto other(const triangulation_edge& edge, const triangulation_vertex* v) -> Point64 {
    return edge.first == v ? edge.second->pt : edge.first->pt;
}
auto invalid_structure() -> void {
    raise_clipper_error(clipper_error_code::invalid_argument);
}
auto source_winding_at_event(const triangulation_context& context, Point64 point,
                             boundary_ray probe) -> int64_t {
    int64_t winding = 0;
    (void)context.source_rings.query(triangulation_bounds(point, point), [&](std::size_t ring) {
        if (triangulation_probe_in_bounds(context.source_rings.bounds(ring),
                                         point, point, probe.x, probe.y)) {
            winding += boundary_winding(context.source_paths[ring], point, point, probe);
        }
        return true;
    });
    return winding;
}
}  // namespace
// A closed y-classification cycle has at most floor(n/2) crossings of either
// sign. This also bounds every local counter prefix.
auto boundary_winding(std::span<const Point64> path, Point64 a, Point64 b, boundary_ray d) noexcept -> int64_t {
    int64_t winding = 0;
    for (std::size_t i = 0; i < path.size(); ++i) {
        const auto p = path[i], q = path[(i + 1) % path.size()];
        const auto py = lexsign(sign((p.y - a.y) + (p.y - b.y)), -sign(d.y));
        const auto qy = lexsign(sign((q.y - a.y) + (q.y - b.y)), -sign(d.y));
        const auto e = ray_to(p, q);
        const boundary_ray twice_offset{(a.x - p.x) + (b.x - p.x), (a.y - p.y) + (b.y - p.y)};
        const auto orientation = lexsign(ray_cross(e, twice_offset), ray_cross(e, d));
        if (py <= 0 && qy > 0 && orientation > 0) { ++winding; }
        if (py > 0 && qy <= 0 && orientation < 0) { --winding; }
    }
    return winding;
}

auto build_boundary_events(triangulation_context& context) -> void {
    // Each emitted event consumes one previously unassigned outgoing edge.
    // Complete preparation assigns every edge, so the final count is known.
    context.boundary_events.reserve(context.edges.size());
    struct ray_group {
        boundary_ray ray;
        std::vector<triangulation_edge*> outgoing, incoming;
    };
    const auto emit = [&](triangulation_vertex* vertex,
                          triangulation_edge* incoming,
                          triangulation_edge* outgoing,
                          int cross) {
        const bool out_later = sweep_before(vertex->pt, outgoing->second->pt);
        const bool in_later = sweep_before(vertex->pt, incoming->first->pt);
        const auto kind = out_later == in_later
                              ? (out_later ? boundary_event_kind::open : boundary_event_kind::close)
                              : boundary_event_kind::continuation;
        const auto id = context.boundary_events.size();
        if (outgoing->first_event != no_boundary_event ||
            incoming->second_event != no_boundary_event) {
            invalid_structure();
        }
        outgoing->first_event = id;
        incoming->second_event = id;
        context.boundary_events.emplace_back(vertex, incoming, outgoing, kind, cross < 0);
    };
    for (auto vertex_ref : context.vertices) {
        auto* vertex = vertex_ref.get();
        vertex->event_begin = context.boundary_events.size();
        // After complete crossing/containment/direction validation, a degree-two
        // vertex with distinct rays has exactly one occupied sector: outgoing
        // to incoming CCW. Contacts, seams and retraces do not meet this proof.
        if (vertex->edges.size() == 2) {
            auto* a = vertex->edges[0].get();
            auto* b = vertex->edges[1].get();
            auto* incoming = a->second == vertex ? a : b;
            auto* outgoing = a->first == vertex ? a : b;
            const auto start = ray_to(vertex->pt, other(*outgoing, vertex));
            const auto end = ray_to(vertex->pt, other(*incoming, vertex));
            if (incoming != outgoing && incoming->second == vertex && outgoing->first == vertex &&
                !same_boundary_ray(start, end)) {
                emit(vertex, incoming, outgoing, ray_cross(start, end));
                vertex->event_end = context.boundary_events.size();
                continue;
            }
        }
        auto incident = std::vector<triangulation_edge_ref>{vertex->edges.begin(), vertex->edges.end()};
        std::sort(incident.begin(), incident.end(), [&](auto a, auto b) {
            return ray_less(ray_to(vertex->pt, other(*a, vertex)),
                            ray_to(vertex->pt, other(*b, vertex)));
        });
        std::vector<ray_group> groups;
        for (auto edge_ref : incident) {
            auto* edge = edge_ref.get();
            const auto ray = ray_to(vertex->pt, other(*edge, vertex));
            if (groups.empty() || !same_boundary_ray(groups.back().ray, ray)) {
                groups.push_back({ray, {}, {}});
            }
            (edge->first == vertex ? groups.back().outgoing : groups.back().incoming)
                .push_back(edge);
        }
        if (groups.size() < 2) { invalid_structure(); }
        vertex->event_begin = context.boundary_events.size();
        for (std::size_t i = 0; i < groups.size(); ++i) {
            const auto& start = groups[i];
            const auto& end = groups[(i + 1) % groups.size()];
            const auto cross = ray_cross(start.ray, end.ray);
            boundary_ray probe{start.ray.x + end.ray.x, start.ray.y + end.ray.y};
            if (cross < 0) {
                probe = {-probe.x, -probe.y};
            } else if (cross == 0) {
                probe = {-start.ray.y, start.ray.x};
            }
            // Normalization admitted one segment vector with S total source points.
            // Each ring-sum prefix is bounded by sum floor(n_r/2) <= floor(S/2).
            const int64_t winding = source_winding_at_event(context, vertex->pt, probe);
            if (winding < 0 || winding > 1) { invalid_structure(); }
            if (!winding) { continue; }
            if (start.outgoing.size() != 1 || end.incoming.size() != 1) { invalid_structure(); }
            auto* outgoing = start.outgoing.front();
            auto* incoming = end.incoming.front();
            emit(vertex, incoming, outgoing, cross);
        }
        vertex->event_end = context.boundary_events.size();
    }
    for (auto edge : context.edges) {
        if (edge->first_event == no_boundary_event || edge->second_event == no_boundary_event) {
            invalid_structure();
        }
    }
}

auto event_for_ray(const triangulation_context& context,
                   const triangulation_vertex* vertex,
                   boundary_ray ray) noexcept -> std::size_t {
    for (auto i = vertex->event_begin; i < vertex->event_end; ++i) {
        if (context.boundary_events[i].sector.contains_ray(ray, false)) { return i; }
    }
    return no_boundary_event;
}
}  // namespace clipper2next::internal
