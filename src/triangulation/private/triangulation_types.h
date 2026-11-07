#pragma once

#include "clipper2next/core.h"
#include "support/private/storage/topology_store.h"
#include "triangulation/private/triangulation_incident_edges.h"
#include "triangulation/private/triangulation_boundary_sector.h"

#include <array>
#include <memory_resource>
#include <vector>

namespace clipper2next::internal {

enum class triangulation_edge_kind { loose, ascend, descend };
enum class triangulation_intersect_kind { none, collinear, intersect };
enum class triangulation_edge_contains_result { neither, left, right };

struct triangulation_triangle;
struct triangulation_vertex;
struct triangulation_vertex_tag;
struct triangulation_triangle_tag;

using triangulation_vertex_ref = topology_ref<triangulation_vertex, triangulation_vertex_tag>;
using triangulation_triangle_ref = topology_ref<triangulation_triangle, triangulation_triangle_tag>;

// Global edge enumeration and the CDT worklist.
using triangulation_edge_list = std::pmr::vector<triangulation_edge_ref>;

struct triangulation_vertex {
    Point64 pt;
    triangulation_incident_edges edges;
    std::size_t event_begin = 0;
    std::size_t event_end = 0;

    explicit triangulation_vertex(
        const Point64& point,
        std::pmr::memory_resource* resource = std::pmr::get_default_resource())
        : pt(point),
          edges(resource) {}

    triangulation_vertex(const triangulation_vertex&) = delete;
    auto operator=(const triangulation_vertex&) -> triangulation_vertex& = delete;
};

inline constexpr std::size_t no_boundary_event = static_cast<std::size_t>(-1);

enum class boundary_event_kind { close, continuation, open };

struct triangulation_edge {
    triangulation_vertex_ref first;
    triangulation_vertex_ref second;
    triangulation_vertex_ref vL;
    triangulation_vertex_ref vR;
    triangulation_vertex_ref vB;
    triangulation_vertex_ref vT;
    triangulation_edge_kind kind = triangulation_edge_kind::loose;
    triangulation_triangle_ref triA;
    triangulation_triangle_ref triB;
    std::size_t first_event = no_boundary_event;
    std::size_t second_event = no_boundary_event;
    bool isActive = false;
    triangulation_edge_ref nextE;
    triangulation_edge_ref prevE;
};

// Source coordinates and constraint endpoints stay fixed after event admission.
// The event owns both its immutable scheduling association and numeric sector.
struct boundary_corner_event final {
    const triangulation_vertex_ref vertex;
    const triangulation_edge_ref incoming;
    const triangulation_edge_ref outgoing;
    const boundary_event_kind kind;
    const bool reflex;
    const boundary_sector sector;

    boundary_corner_event(triangulation_vertex_ref vertex_source,
                          triangulation_edge_ref incoming_source,
                          triangulation_edge_ref outgoing_source,
                          boundary_event_kind event_kind, bool reflex_source) noexcept
        : vertex(vertex_source), incoming(incoming_source), outgoing(outgoing_source),
          kind(event_kind), reflex(reflex_source),
          sector(vertex_source->pt, outgoing_source->second->pt, incoming_source->first->pt) {}
};

struct triangulation_triangle {
    std::array<triangulation_edge_ref, 3> edges{};
    std::int8_t orientation{};

    triangulation_triangle(triangulation_edge_ref e1,
                           triangulation_edge_ref e2,
                           triangulation_edge_ref e3) {
        edges[0] = e1;
        edges[1] = e2;
        edges[2] = e3;
    }
};

using triangulation_vertex_list = std::vector<triangulation_vertex_ref>;
using triangulation_triangle_list = std::vector<triangulation_triangle_ref>;

}  // namespace clipper2next::internal
