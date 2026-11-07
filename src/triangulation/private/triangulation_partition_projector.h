#pragma once

#include "clipper2next/api/error.h"
#include "triangulation/private/triangulation_context.h"

#include <utility>

namespace clipper2next::internal {

class partition_face_projector final {
public:
    partition_face_projector(triangulation_context& context,
                             const triangulation_predicate_domain& domain) noexcept
        : context_(context), domain_(domain) {}

    auto orientation(std::size_t a, std::size_t b, std::size_t c) const noexcept -> int {
        const auto& path = context_.partition_path;
        return domain_.orientation(path[a], path[b], path[c]);
    }

    auto face(std::size_t a, std::size_t b, std::size_t c) -> void {
        const auto& vertices = context_.partition_vertices;
        const auto count = vertices.size();
        auto left = vertices[a], right = vertices[b];
        // An existing contour or partition side owns its x-tie order.
        triangulation_edge_ref side;
        if ((a + 1U) % count == b) { side = context_.partition_sides[a]; }
        else if ((b + 1U) % count == a) { side = context_.partition_sides[b]; }
        if (side) {
            left = side->vL;
            right = side->vR;
        } else {
            // The root has not appeared in earlier fans. Within this fan
            // the next base is the previous third side, directed (b,a).
            if (a == previous_root_ && b == previous_third_) { std::swap(left, right); }
            if (left->pt.x > right->pt.x) { std::swap(left, right); }
        }
        auto third = vertices[c];
        const auto turn = domain_.orientation(left->pt, right->pt, third->pt);
        if (!turn) { raise_clipper_error(clipper_error_code::internal_error); }
        if (turn < 0) { std::swap(left, third); }
        context_.partition_faces.push_back({left, right, third});
        previous_root_ = a;
        previous_third_ = c;
    }

private:
    triangulation_context& context_;
    const triangulation_predicate_domain& domain_;
    std::size_t previous_root_ = no_boundary_event;
    std::size_t previous_third_ = no_boundary_event;
};

}  // namespace clipper2next::internal
