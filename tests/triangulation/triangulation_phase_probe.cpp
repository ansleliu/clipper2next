// Test-only phase cost probe. Calls the actual private pipeline; no TLS result
// cache and no performance acceptance threshold are involved.
#include "triangulation/private/triangulation_path_builder.h"
#include "triangulation/private/triangulation_graph_repair.h"
#include "triangulation/private/triangulation_boundary_events.h"
#include "triangulation/private/triangulation_sweep_line.h"
#include "triangulation/private/triangulation_delaunay.h"
#include "triangulation/private/triangulation_result_builder.h"
#include <array>
#include <chrono>
#include <iostream>

namespace next = clipper2next;
namespace internal = next::internal;

auto measure(const char* name, const next::Paths64& paths, std::size_t repeats) -> void {
    std::array<double, 6> nanos{};
    internal::triangulation_context context;
    for (std::size_t i = 0; i < repeats; ++i) {
        context.clear();
        auto last = std::chrono::steady_clock::now();
        const auto phase = [&](std::size_t index) {
            const auto now = std::chrono::steady_clock::now();
            nanos[index] += std::chrono::duration<double, std::nano>(now - last).count();
            last = now;
        };
        if (!context.source_paths.append(paths)) {
            throw std::runtime_error{"empty phase input"};
        }
        phase(0);
        if (internal::normalize_triangulation_constraints(context) !=
            next::TriangulateResult::success) {
            throw std::runtime_error{"invalid phase input"};
        }
        phase(1);
        internal::build_boundary_events(context);
        phase(2);
        if (internal::run_triangulation_sweep(context, clipper2next::internal::prepare_triangulation_predicate_domain(context)) != next::TriangulateResult::success) {
            throw std::runtime_error{"phase sweep failed"};
        }
        phase(3);
        if (!internal::legalize_pending_delaunay_edges(context, clipper2next::internal::prepare_triangulation_predicate_domain(context))) {
            throw std::runtime_error{"phase CDT failed"};
        }
        phase(4);
        const auto triangles = internal::build_triangulation_result(internal::validate_triangulation_mesh(context, clipper2next::internal::prepare_triangulation_predicate_domain(context)));
        if (triangles.empty()) { throw std::runtime_error{"phase mesh empty"}; }
        phase(5);
    }
    std::cout << name << " repeats=" << repeats;
    constexpr std::array<const char*, 6> names{
        "source_ns", "normalize_roles_ns", "events_ns", "sweep_ns", "CDT_ns", "result_ns"};
    for (std::size_t i = 0; i < nanos.size(); ++i) {
        std::cout << ' ' << names[i] << '=' << nanos[i] / repeats;
    }
    std::cout << '\n';
}
auto main(int argc, char**) -> int {
    try {
        measure("ordinary-eight",
                {{{0, 0}, {20, 0}, {30, 10}, {30, 20}, {20, 30}, {0, 30}, {-10, 20}, {-10, 10}}},
                10000);
        measure("T-touch", {{{0, 0}, {10, 0}, {10, 10}, {0, 10}}, {{0, 5}, {2, 7}, {2, 3}}}, 10000);
        if (argc == 2) {
            std::size_t path_count = 0;
            if (!(std::cin >> path_count)) { return 2; }
            next::Paths64 paths(path_count);
            for (auto& path : paths) {
                std::size_t count = 0;
                if (!(std::cin >> count)) { return 2; }
                path.resize(count);
                for (auto& point : path) {
                    if (!(std::cin >> point.x >> point.y)) { return 2; }
                }
            }
            measure("external-input", paths, 20);
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
