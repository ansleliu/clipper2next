#include "triangulation/private/triangulation_simple.h"
#include "triangulation/private/triangulation_monotone_graph.h"
#include "triangulation/private/triangulation_operation.h"
#include "support/private/checked_size.h"
#include "support/private/paths_rebuild.h"
#include <array>
#include <cassert>
#include <type_traits>

namespace clipper2next::internal {
namespace {

template <class Emit>
class monotone_result_builder final {
    const Path64& path_;
    int direction_;
    const triangulation_predicate_domain& domain_;
    Emit emit_;
    std::size_t previous_root_ = no_boundary_event;
    std::size_t previous_third_ = no_boundary_event;

    auto point(std::size_t i) const noexcept -> Point64 {
        return path_[direction_ > 0 ? i : path_.size() - 1U - i];
    }

public:
    monotone_result_builder(const Path64& path, int direction,
                            const triangulation_predicate_domain& domain, Emit emit)
        : path_(path), direction_(direction), domain_(domain), emit_(emit) {}

    auto orientation(std::size_t a, std::size_t b, std::size_t c) const noexcept -> int {
        return domain_.orientation(point(a), point(b), point(c));
    }

    auto face(std::size_t a, std::size_t b, std::size_t c) -> void {
        auto first = a;
        // Boundary sides precede every fan. A subsequent fan base was the
        // previous face's (b,a) third side. Preserve its x-tie endpoint owner.
        if ((b + 1U) % path_.size() == a) { first = b; }
        else if ((a + 1U) % path_.size() != b && previous_root_ == a) {
            assert(previous_third_ == b);
            first = b;
        }
        const auto second = first == a ? b : a;
        auto left = point(first), right = point(second);
        const auto third = point(c);
        if (left.x > right.x) { std::swap(left, right); }
        const auto turn = domain_.orientation(left, right, third);
        if (!turn) { raise_clipper_error(clipper_error_code::internal_error); }
        if (turn > 0) { emit_(left, right, third); }
        else { emit_(third, right, left); }
        previous_root_ = a;
        previous_third_ = c;
    }
};

template <class C>
auto build_simple_result(triangulation_context& context, const Paths64& paths,
                          Paths<C>& output, double scale) -> bool {
    if (paths.size() != 1U || paths.front().size() < 3U) { return false; }
    // The operation owns these coordinates through allocation and reentry.
    auto& path = context.partition_path;
    path = paths.front();
    const auto domain = triangulation_predicate_domain::from_points(
        path.size(), [&](std::size_t i) { return path[i]; });
    const auto layout = prepare_monotone_layout(path, domain, context.monotone);
    if (!layout) { return false; }

    // Complete the same nondegeneracy/count checks before preparing caller
    // buffers. The second walk cannot allocate or change its admitted source.
    std::size_t faces = 0;
    monotone_result_builder validate{path, layout->direction, domain,
        [&](Point64, Point64, Point64) { ++faces; }};
    walk_monotone_faces(context.monotone, layout->order, validate);
    if (faces != path.size() - 2U) { raise_clipper_error(clipper_error_code::internal_error); }

    paths_rebuild<C> transaction{output};
    transaction.prepare(faces, [](auto) { return 3U; });
    transaction.begin();
    std::size_t index = 0;
    monotone_result_builder project{path, layout->direction, domain,
        [&](Point64 a, Point64 b, Point64 c) {
            auto& triangle = output[index++];
            for (const auto point : {a, b, c}) {
                if constexpr (std::is_same_v<C, int64_t>) { triangle.push_back(point); }
                else { triangle.emplace_back(point.x * scale, point.y * scale); }
            }
        }};
    walk_monotone_faces(context.monotone, layout->order, project);
    transaction.finish();
    return true;
}

template <class C>
auto project_cocircular_quad(triangulation_operation_lease& operation, const Path64& input,
                             Paths<C>& output, double scale) -> bool {
    assert(input.size() == 4U);
    auto& context = operation.context();
    auto& path = context.partition_path;
    path = input;
    const auto domain = triangulation_predicate_domain::from_points(
        path.size(), [&](std::size_t i) { return path[i]; });
    // Keep the general graph path when certification requires a wider predicate.
    if (domain.precision() != circle_precision::exact64 ||
        oriented_in_circle(path[0], path[1], path[2], path[3], domain).circle != 0) {
        return false;
    }
    const auto layout = prepare_monotone_layout(path, domain, context.monotone);
    if (!layout) { return false; }

    // The same monotone walk and canonical corner order as Sweep. On this
    // certified ring every ordering of the four corners gives the exact tie,
    // so the CDT legalizer would leave the sole interior edge untouched.
    std::array<std::array<Point64, 3>, 2> faces;
    std::size_t count = 0;
    monotone_result_builder collect{path, layout->direction, domain,
        [&](Point64 a, Point64 b, Point64 c) {
            if (count == faces.size()) { raise_clipper_error(clipper_error_code::internal_error); }
            faces[count++] = {a, b, c};
        }};
    walk_monotone_faces(context.monotone, layout->order, collect);
    if (count != faces.size()) { raise_clipper_error(clipper_error_code::internal_error); }

    paths_rebuild<C> transaction{output};
    transaction.prepare(faces.size(), [](auto) { return 3U; });
    transaction.begin();
    for (std::size_t i = 0; i < faces.size(); ++i) {
        for (const auto& point : faces[i]) {
            if constexpr (std::is_same_v<C, int64_t>) { output[i].push_back(point); }
            else { output[i].emplace_back(point.x * scale, point.y * scale); }
        }
    }
    transaction.finish();
    return true;
}

}  // namespace

auto try_build_simple_triangulation_graph(triangulation_context& context,
                                         const Paths64& paths)
    -> std::optional<triangulation_predicate_domain> {
    if (paths.size() != 1 || paths.front().size() < 3) { return std::nullopt; }
    const auto& path = paths.front();
    const auto domain = triangulation_predicate_domain::from_points(
        path.size(), [&](std::size_t i) { return path[i]; });
    const auto layout = prepare_monotone_layout(path, domain, context.monotone);
    if (!layout) { return std::nullopt; }
    const auto count = path.size();
    // The certified ring owns N vertices, 2N-3 sides and N-2 faces.
    // Prepare their directories once before publishing any graph nodes.
    context.vertices.reserve(count);
    context.edges.reserve(checked_size_add(count, count - 3U));
    context.triangles.reserve(count - 2U);
    for (std::size_t i = 0; i != count; ++i) {
        (void)create_triangulation_vertex(
            context, path[layout->direction > 0 ? i : count - 1 - i]);
    }
    for (std::size_t i = 0; i != count; ++i) {
        auto* a = context.vertices[i].get();
        auto* b = context.vertices[(i + 1) % count].get();
        (void)create_triangulation_edge(
            context, a, b,
            sweep_before(a->pt, b->pt) ? triangulation_edge_kind::ascend
                                      : triangulation_edge_kind::descend);
    }
    build_monotone_faces(context, context.vertices, layout->order, domain);
    return domain;
}

auto try_build_simple_triangulation_result(triangulation_context& context,
                                           const Paths64& paths, Paths64& result) -> bool {
    return build_simple_result(context, paths, result, 1.0);
}

auto try_build_simple_triangulation_result(triangulation_context& context,
                                           const Paths64& paths, PathsD& result, double scale) -> bool {
    return build_simple_result(context, paths, result, scale);
}

auto try_project_cocircular_quad(triangulation_operation_lease& operation, const Path64& path,
                                 Paths64& output) -> bool {
    return project_cocircular_quad(operation, path, output, 1.0);
}

auto try_project_cocircular_quad(triangulation_operation_lease& operation, const Path64& path,
                                 PathsD& output, double scale) -> bool {
    return project_cocircular_quad(operation, path, output, scale);
}

}  // namespace clipper2next::internal
