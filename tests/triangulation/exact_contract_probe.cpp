#include "clipper2next/triangulation.h"
#include "support/triangulation_contract.h"
#include "triangulation/private/triangulation_exact_predicates.h"
#include "triangulation/private/triangulation_intersections.h"
#include "triangulation/private/triangulation_boundary_events.h"
#include "geometry/private/geometry_exact.h"

#include <array>
#include <iostream>
#include <sstream>
#include <string>

namespace next = clipper2next;

// One request/response per line; P is point squared distance and C compares two P pairs.
// Predicate coordinates must be in the admitted [-MAX_COORD, MAX_COORD] domain.
auto main() -> int {
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream input(line);
        std::ostringstream output;
        char operation = 0;
        if (!(input >> operation)) { return 2; }
        if (operation == 'T') {
            int delaunay = 0;
            std::size_t path_count = 0;
            if (!(input >> delaunay >> path_count) || delaunay < 0 || delaunay > 1 ||
                path_count > 10000) {
                return 2;
            }
            next::triangulation_request64 request;
            request.use_delaunay = delaunay != 0;
            request.paths.resize(path_count);
            for (auto& path : request.paths) {
                std::size_t count = 0;
                if (!(input >> count) || count > 100000) { return 2; }
                path.resize(count);
                for (auto& point : path) {
                    if (!(input >> point.x >> point.y)) { return 2; }
                }
            }
            const auto result = next::triangulate_checked(request);
            if (!result) {
                output << "ERR " << static_cast<int>(result.error());
            } else {
                output << "OK " << static_cast<int>(result->status) << ' '
                       << result->triangles.size();
                for (const auto& triangle : result->triangles) {
                    if (triangle.size() != 3) { return 3; }
                    for (const auto& point : triangle) {
                        output << ' ' << point.x << ' ' << point.y;
                    }
                }
            }
        } else if (operation == 'M' || operation == 'R') {
            std::array<int64_t, 4> values;
            for (auto& value : values) { if (!(input >> value)) { return 2; } }
            if (operation == 'M') {
                output << next::internal::geometry_exact::compare_signed_products(values[0], values[1], values[2], values[3]);
            } else {
                output << next::internal::ray_cross({values[0], values[1]}, {values[2], values[3]});
            }
        } else {
            const std::size_t count = operation == 'P' ? 2U : 4U;
            if (operation != 'P' && operation != 'C' && operation != 'I' && operation != 'S') {
                return 2;
            }
            std::array<next::Point64, 4> points;
            for (std::size_t i = 0; i < count; ++i) {
                if (!next::test_support::read_exact_point(input, points[i])) { return 2; }
            }
            if (operation == 'I') {
                const auto domain=next::internal::triangulation_predicate_domain::from_points(4U,
                    [&](std::size_t i){return points[i];});
                const auto result = next::internal::oriented_in_circle(
                    points[0], points[1], points[2], points[3], domain);
                output << result.circle << ' ' << result.orientation;
            } else if (operation == 'S') {
                output << static_cast<int>(
                    next::internal::SegsIntersect(points[0], points[1], points[2], points[3]));
            } else {
                const auto first =
                    next::internal::squared_point_distance_in_clipper_range(points[0], points[1]);
                if (operation == 'C') {
                    const auto second = next::internal::squared_point_distance_in_clipper_range(
                        points[2], points[3]);
                    output << next::internal::compare_squared_distance(first, second);
                } else {
                    output << first.squared[0] << ' ' << first.squared[1];
                }
            }
        }
        input >> std::ws;
        if (!input.eof()) { return 2; }
        std::cout << output.str() << '\n';
    }
    return std::cin.bad() ? 2 : 0;
}
