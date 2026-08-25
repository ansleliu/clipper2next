#pragma once

#include "clipper2next/core.h"
#include "support/private/storage/topology_store.h"

#include <array>
#include <memory_resource>
#include <vector>

namespace clipper2next::internal {

enum class triangulation_edge_kind { loose, ascend, descend };
enum class triangulation_intersect_kind { none, collinear, intersect };
enum class triangulation_edge_contains_result { neither, left, right };

struct triangulation_edge;
struct triangulation_triangle;
struct triangulation_vertex;
struct triangulation_vertex_tag;
struct triangulation_edge_tag;
struct triangulation_triangle_tag;

using triangulation_vertex_ref = topology_ref<triangulation_vertex, triangulation_vertex_tag>;
using triangulation_edge_ref = topology_ref<triangulation_edge, triangulation_edge_tag>;
using triangulation_triangle_ref = topology_ref<triangulation_triangle, triangulation_triangle_tag>;
using triangulation_edge_list = std::pmr::vector<triangulation_edge_ref>;

struct triangulation_vertex {
    Point64 pt;
    triangulation_edge_list edges;
    bool innerLM = false;

    explicit triangulation_vertex(
        const Point64& point,
        std::pmr::memory_resource* resource = std::pmr::get_default_resource())
        : pt(point),
          edges(resource) {
        edges.reserve(2);
    }

    triangulation_vertex(const triangulation_vertex&) = delete;
    auto operator=(const triangulation_vertex&) -> triangulation_vertex& = delete;
};

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
    bool isActive = false;
    triangulation_edge_ref nextE;
    triangulation_edge_ref prevE;
};

struct triangulation_triangle {
    std::array<triangulation_edge_ref, 3> edges{};

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
