#include "triangulation/private/triangulation_coordinate_identity.h"
#include <iostream>
#include <map>
#include <random>
#include <stdexcept>

namespace in = clipper2next::internal;
using clipper2next::Point64;

auto require(bool value) -> void {
    if (!value) { throw std::runtime_error{"coordinate identity contract failed"}; }
}

auto main() -> int {
    try {
        in::triangulation_context context;
        std::vector<Point64> points;
        const auto maximum = (std::int64_t{1} << 61) - 1;
        std::mt19937_64 random{20261008};
        for (const auto extent : {std::int64_t{16}, std::int64_t{1} << 32, maximum}) {
            std::uniform_int_distribution<std::int64_t> coordinate{-extent, extent};
            for (std::size_t i = 0; i != 2000; ++i) {
                const Point64 point{coordinate(random), coordinate(random)};
                points.push_back(point);
                points.push_back(point);
                points.push_back({point.y, point.x});
            }
        }
        in::triangulation_coordinate_identity identity{points.size()};
        std::map<std::pair<std::int64_t, std::int64_t>, in::triangulation_vertex*> expected;
        for (const auto point : points) {
            auto* actual = identity.vertex(context, point);
            auto [entry, inserted] = expected.try_emplace({point.x, point.y}, actual);
            require(entry->second == actual && actual->pt == point);
            require(context.vertices.size() == expected.size());
            if (inserted) { require(context.vertices.back().get() == actual); }
        }
        for (const auto count : {
                 (std::size_t{1} << (std::numeric_limits<std::size_t>::digits - 2)) + 1U,
                 std::numeric_limits<std::size_t>::max()}) {
            bool rejected = false;
            try { in::triangulation_coordinate_identity invalid{count}; }
            catch (const std::length_error&) { rejected = true; }
            require(rejected);
        }
        std::cout << "coordinate-identity-contract=PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
