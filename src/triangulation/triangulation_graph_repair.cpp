#include "triangulation/private/triangulation_graph_repair.h"
#include "triangulation/private/triangulation_source_segment.h"
#include "triangulation/private/triangulation_coordinate_identity.h"
#include "triangulation/private/triangulation_boundary_events.h"
#include "geometry/private/geometry_segment_query.h"
#include "support/private/checked_size.h"
#include "geometry/private/integer_area_sign.h"
#include <algorithm>
#include <cassert>
#include <span>

namespace clipper2next::internal {
namespace {
using triangulation_graph_repair_detail::containment_rows;
using triangulation_graph_repair_detail::indexed_relation;
using ring_contact = std::pair<std::size_t, std::size_t>;

auto bounds_contain(const Rect64& outer, const Rect64& inner) noexcept -> bool {
    return outer.left <= inner.left && outer.top <= inner.top &&
           outer.right >= inner.right && outer.bottom >= inner.bottom;
}

auto invalid_structure() -> void {
    raise_clipper_error(clipper_error_code::invalid_argument);
}

auto materialize_constraints(triangulation_context& context,
                             const std::vector<triangulation_source_segment>& segments, int root_sign) -> void {
    triangulation_coordinate_identity vertices{segments.size()};
    triangulation_vertex* previous = nullptr;
    auto ring = no_boundary_event;
    // Splitting preserves contour order and each preceding endpoint.
    for (const auto& source : segments) {
        const auto splits = source.points();
        if (ring != source.ring()) {
            previous = vertices.vertex(context, splits.front());
            ring = source.ring();
        }
        assert(previous->pt == splits.front());
        for (std::size_t k = 1; k < splits.size(); ++k) {
            auto* next = vertices.vertex(context, splits[k]);
            auto* a = previous;
            auto* b = next;
            if (root_sign < 0) { std::swap(a, b); }
            (void)create_triangulation_edge(context, a, b,
                sweep_before(a->pt, b->pt) ? triangulation_edge_kind::ascend : triangulation_edge_kind::descend);
            previous = next;
        }
    }
}

auto source_segments(const triangulation_source_paths& paths) -> std::vector<triangulation_source_segment> {
    std::vector<triangulation_source_segment> segments;
    segments.reserve(paths.point_count());
    for (std::size_t ring = 0; ring < paths.size(); ++ring) {
        const auto path = paths[ring];
        for (std::size_t i = 0; i < path.size(); ++i) {
            const auto a = path[i], b = path[(i + 1) % path.size()];
            segments.emplace_back(a, b, ring);
        }
    }
    return segments;
}
auto split_intersections(std::vector<triangulation_source_segment>& segments,
                         std::vector<ring_contact>& contacts,
                         const triangulation_predicate_domain& domain) -> bool {
    triangulation_bounds_catalog catalog;
    catalog.build(segments.size(), [&](std::size_t i) {
        return triangulation_bounds(segments[i].first(), segments[i].second());
    });
    const bool valid=catalog.query_pairs([&](std::size_t i,std::size_t j) {
        auto& a=segments[i];
        auto& b=segments[j];
        const geometry_segment_query query{a.first(),a.second(),domain.orientation_fits_signed64(),catalog.bounds(i)};
        const auto facts=query.contacts(b.first(),b.second(),catalog.bounds(j));
        if (facts.proper_crossing) { return false; }
        const bool a0=facts.first_on_target,a1=facts.second_on_target;
        const bool b0=facts.target_first_on_query,b1=facts.target_second_on_query;
        if (a0) { b.add_split(a.first()); }
        if (a1) { b.add_split(a.second()); }
        if (b0) { a.add_split(b.first()); }
        if (b1) { a.add_split(b.second()); }
        if (a.ring()!=b.ring() && (a0 || a1 || b0 || b1)) {
            contacts.emplace_back(std::min(a.ring(),b.ring()),std::max(a.ring(),b.ring()));
        }
        return true;
    });
    if (!valid) { return false; }
    std::sort(contacts.begin(), contacts.end());
    contacts.erase(std::unique(contacts.begin(), contacts.end()), contacts.end());
    return true;
}
auto source_signs(const triangulation_source_paths& paths) -> std::vector<int> {
    std::vector<int> signs;
    signs.reserve(paths.size());
    for (std::size_t i = 0; i != paths.size(); ++i) {
        const auto sign = signed_area_sign(paths[i]);
        if (!sign) { invalid_structure(); }
        signs.push_back(sign);
    }
    return signs;
}
auto order_splits(std::vector<triangulation_source_segment>& segments) -> void {
    for (auto& segment : segments) { segment.order_splits(); }
}
auto possible_relations(const triangulation_bounds_catalog& catalog,
                        const std::vector<ring_contact>& contacts, std::size_t count) -> containment_rows {
    std::vector<indexed_relation> candidates;
    (void)catalog.query_pairs([&](std::size_t ring,std::size_t other) {
        const bool contact = std::binary_search(contacts.begin(), contacts.end(), ring_contact{ring, other});
        // Without boundary crossing or contact, a connected contour is wholly
        // inside or outside the other. Containment requires whole-box inclusion.
        if (contact || bounds_contain(catalog.bounds(other), catalog.bounds(ring))) {
            candidates.push_back({ring, {other, -1, contact}});
        }
        if (contact || bounds_contain(catalog.bounds(ring), catalog.bounds(other))) {
            candidates.push_back({other, {ring, -1, contact}});
        }
        return true;
    });
    return containment_rows{count, candidates};
}
auto noncontact_winding(std::span<const Point64> path, Point64 point,
                        const triangulation_predicate_domain& domain) noexcept -> int64_t {
    int64_t winding = 0;
    auto previous = path.back();
    for (const auto current : path) {
        if (previous.y <= point.y) {
            if (current.y > point.y && domain.orientation(previous, current, point) > 0) { ++winding; }
        } else if (current.y <= point.y && domain.orientation(previous, current, point) < 0) {
            --winding;
        }
        previous = current;
    }
    return winding;
}

auto measure_relations(const triangulation_context& context, const std::vector<triangulation_source_segment>& segments,
                       const std::vector<int>& signs, containment_rows& rows,
                       const triangulation_predicate_domain& domain) -> void {
    for (const auto& segment : segments) {
        const auto splits = segment.points();
        const auto delta = ray_to(segment.first(), segment.second());
        const boundary_ray normal{-delta.y * signs[segment.ring()], delta.x * signs[segment.ring()]};
        for (auto& relation : rows[segment.ring()]) {
            if (!relation.contact) {
                // Complete contact enumeration proves this endpoint is strictly
                // off the other boundary, with constant winding along this ring.
                // The existing point domain suffices; no symbolic normal is needed.
                if (relation.value == -1) {
                    relation.value = noncontact_winding(context.source_paths[relation.ring],
                                                        segment.first(), domain) != 0;
                }
                continue;
            }
            for (std::size_t k = 1; k < splits.size(); ++k) {
                const auto a = splits[k - 1], b = splits[k];
                const int value = triangulation_probe_in_bounds(context.source_rings.bounds(relation.ring),
                    a, b, normal.x, normal.y) ? boundary_winding(context.source_paths[relation.ring], a, b, normal) != 0 : 0;
                if (relation.value != -1 && relation.value != value) { invalid_structure(); }
                relation.value = value;
            }
        }
    }
}
auto relation_value(const containment_rows& rows, std::size_t ring, std::size_t other) -> int {
    const auto& row = rows[ring];
    const auto found = std::lower_bound(row.begin(), row.end(), other,
        [](const auto& item, std::size_t value) { return item.ring < value; });
    return found != row.end() && found->ring == other ? found->value : 0;
}
auto root_direction(const containment_rows& rows, const std::vector<int>& signs) -> int {
    int root_sign = 0;
    for (std::size_t ring = 0; ring < rows.size(); ++ring) {
        std::size_t depth = 0;
        for (const auto& ancestor : rows[ring]) {
            if (ancestor.value != 1) { continue; }
            const auto s = ancestor.ring;
            if (relation_value(rows, s, ring) == 1) { invalid_structure(); }
            ++depth;
            for (const auto& other : rows[ring]) {
                const auto t = other.ring;
                if (t > s && other.value == 1 && relation_value(rows, s, t) != 1 && relation_value(rows, t, s) != 1) {
                    invalid_structure();
                }
            }
        }
        const int implied = depth % 2 ? -signs[ring] : signs[ring];
        if (root_sign && implied != root_sign) { invalid_structure(); }
        root_sign = implied;
    }
    return root_sign;
}
auto ring_bounds(std::span<const Point64> path) -> Rect64 {
    auto bounds = triangulation_bounds(path.front(), path.front());
    for (const auto point : path) {
        bounds.left = std::min(bounds.left, point.x); bounds.right = std::max(bounds.right, point.x);
        bounds.top = std::min(bounds.top, point.y); bounds.bottom = std::max(bounds.bottom, point.y);
    }
    return bounds;
}
} // namespace

auto normalize_triangulation_constraints(triangulation_context& context) -> TriangulateResult {
    auto& paths = context.source_paths;
    if (paths.empty()) { return TriangulateResult::no_polygons; }
    context.source_rings.build(paths.size(), [&](std::size_t i) { return ring_bounds(paths[i]); });
    auto segments = source_segments(paths);
    const auto domain = triangulation_predicate_domain::from_points(
        checked_size_multiply(segments.size(), 2U), [&](std::size_t i) {
            return i % 2U ? segments[i / 2U].second() : segments[i / 2U].first();
        });
    std::vector<ring_contact> contacts;
    if (!split_intersections(segments, contacts, domain)) { return TriangulateResult::paths_intersect; }
    const auto signs = source_signs(paths);
    order_splits(segments);
    auto relations = possible_relations(context.source_rings, contacts, paths.size());
    measure_relations(context, segments, signs, relations, domain);
    const auto direction = root_direction(relations, signs);
    if (direction < 0) {
        for (std::size_t i = 0; i != paths.size(); ++i) {
            const auto path = paths.mutable_path(i);
            std::reverse(path.begin(), path.end());
        }
    }
    materialize_constraints(context, segments, direction);
    return TriangulateResult::success;
}
} // namespace clipper2next::internal
