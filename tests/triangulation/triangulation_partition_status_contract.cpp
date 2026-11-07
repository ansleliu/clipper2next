#include "triangulation/private/triangulation_partition_status.h"
#include "triangulation/private/triangulation_context.h"
#include <algorithm>
#include <iostream>
#include <numeric>
#include <random>
#include <set>
#include <stdexcept>

namespace in = clipper2next::internal;
using clipper2next::Point64;

auto require(bool value) -> void {
    if (!value) { throw std::runtime_error{"partition status contract failed"}; }
}

auto exercise(std::int64_t stride, Point64 shift) -> void {
    constexpr std::size_t count = 127;
    in::triangulation_context context;
    std::vector<in::triangulation_edge_ref> sources;
    for (std::size_t i = 0; i != count; ++i) {
        const auto x = shift.x + static_cast<std::int64_t>(2 * i) * stride;
        auto* a = in::create_triangulation_vertex(context, {x, shift.y + 20});
        auto* b = in::create_triangulation_vertex(context, {x, shift.y});
        sources.push_back(in::create_triangulation_edge(context, a, b));
    }
    const auto domain = in::triangulation_predicate_domain::from_points(
        context.vertices.size() + 2, [&](std::size_t i) {
            if (i < context.vertices.size()) { return context.vertices[i]->pt; }
            return Point64{
                shift.x + (i == context.vertices.size() ? -2 : 258) * stride,
                shift.y + 10,
            };
        });
    in::triangulation_partition_status status{sources, domain};
    std::set<std::size_t> expected;
    const auto check = [&] {
        require(status.empty() == expected.empty());
        for (std::size_t query = 0; query != count; ++query) {
            const Point64 point{
                shift.x + static_cast<std::int64_t>(2 * query + 1) * stride,
                shift.y + 10,
            };
            const auto found = expected.upper_bound(query);
            const auto reference = found == expected.begin()
                ? in::no_boundary_event : *std::prev(found);
            require(status.left(point) == reference);
        }
        for (const auto id : expected) { require(status.helper(id) == id + count); }
    };
    std::vector<std::size_t> order(count);
    std::iota(order.begin(), order.end(), 0);
    std::mt19937 random{20261008};
    for (std::size_t pass = 0; pass != 4; ++pass) {
        std::shuffle(order.begin(), order.end(), random);
        for (const auto id : order) {
            status.insert(id, id + count);
            expected.insert(id);
            check();
        }
        std::shuffle(order.begin(), order.end(), random);
        for (const auto id : order) {
            status.set_helper(id, id);
            require(status.helper(id) == id);
            status.erase(id);
            expected.erase(id);
            check();
        }
        require(status.empty());
    }
}

auto main() -> int {
    try {
        exercise(1, {});
        exercise(std::int64_t{1} << 50, {std::int64_t{1} << 60, -(std::int64_t{1} << 60)});
        std::cout << "partition-status-contract=PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
