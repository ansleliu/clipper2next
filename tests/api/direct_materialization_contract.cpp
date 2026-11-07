#include "clipper2next/clip.h"
#include "clipper2next/offset.h"
#include "clipper2next/offset/builder.h"
#include "clipper2next/rectclip.h"
#include "clipper2next/triangulation.h"
#include "clipper2next/api/memory.h"
#include "clipper2next/core/path_set_builder.h"
#include "clip/engine/private/polygon_tree_rebuild_access.h"
#include "clip/private/clip_execution_strategy.h"
#include "api/private/borrowed_topology_pipeline.h"
#include "support/private/paths_rebuild.h"
#include "rectclip/private/rectclip_facade_runner.h"
#include "support/allocation_failure_probe.h"
#include "support/rectangle_fast_path_contract.h"
#include "support/rectclip_test_execution.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <stdexcept>

namespace n = clipper2next;
namespace in = n::internal;
namespace probe = n::tests;
using probe::ScopedAllocationFailure;
namespace {
auto require(bool value, const char* label) -> void {
    if (!value) {
        std::fprintf(stderr, "FAIL: %s\n", label);
        std::exit(1);
    }
}
auto same(const n::Paths64& a, const n::Paths64& b) -> bool {
    return a == b;
}
auto same(const n::paths64_result& a, const n::paths64_result& b) -> bool {
    return a.closed == b.closed && a.open == b.open;
}
auto same(const n::rect_clip_result64& a, const n::rect_clip_result64& b) -> bool {
    return a.paths == b.paths;
}
auto same(const n::triangulation_result64& a, const n::triangulation_result64& b) -> bool {
    return a.status == b.status && a.triangles == b.triangles;
}
auto same(const n::triangulation_resultd& a, const n::triangulation_resultd& b) -> bool {
    return a.status == b.status && a.triangles == b.triangles;
}
auto same(const n::PolyTree64& a, const n::PolyTree64& b) -> bool {
    using access = n::polygon_tree_rebuild_access;
    if (a.scale() != b.scale() || access::size(a) != access::size(b)) { return false; }
    for (std::size_t i = 0; i < access::size(a); ++i) {
        const auto id = n::PolyTree64::node_id{i};
        if (a.polygon(id) != b.polygon(id) || a.parent(id) != b.parent(id) ||
            a.depth(id) != b.depth(id) || a.is_hole(id) != b.is_hole(id) ||
            !std::ranges::equal(a.children(id), b.children(id))) {
            return false;
        }
    }
    return true;
}
auto same(const n::clip_tree64_result& a, const n::clip_tree64_result& b) -> bool {
    return same(a.tree, b.tree) && a.open == b.open;
}
auto empty(const n::Paths64& value) -> bool {
    return value.empty();
}
auto empty(const n::PolyTree64& value) -> bool {
    return n::polygon_tree_rebuild_access::size(value) == 1 && value.count() == 0 &&
           value.polygon(value.root()).empty();
}
auto rootless_observers_reject(const n::PolyTree64& tree) -> void {
    auto rejects = [](auto observer) {
        bool threw = false;
        try {
            observer();
        } catch (const std::out_of_range&) { threw = true; }
        require(threw, "rootless public observer retains safe rejection");
    };
    rejects([&] { static_cast<void>(tree.count()); });
    rejects([&] { static_cast<void>(tree.children(tree.root())); });
    rejects([&] { static_cast<void>(tree.polygon(tree.root())); });
}
template <typename T, typename Run>
auto matrix(const char* name, const T& sentinel, Run run, bool builder = false) -> void {
    T expected = sentinel;
    run(expected);
    std::size_t throwing = 0, recovered = 0;
    for (std::ptrdiff_t ordinal = 0; ordinal < 2048; ++ordinal) {
        n::release_thread_caches();
        T output = sentinel;
        bool failed = false;
        {
            ScopedAllocationFailure fail{ordinal};
            try {
                run(output);
            } catch (const std::bad_alloc&) { failed = true; }
        }
        const auto hits = probe::rejected_allocation_count();
        const auto family = probe::failed_allocation_family();
        if (failed) {
            ++throwing;
            if constexpr (std::is_same_v<T, n::clip_tree64_result>) {
                if (n::polygon_tree_rebuild_access::size(sentinel.tree) == 0) {
                    rootless_observers_reject(output.tree);
                }
            }
            if constexpr (requires { empty(output); }) {
                require(builder ? empty(output) : same(output, sentinel), name);
            } else {
                require(same(output, sentinel), name);
            }
        } else {
            require(same(output, expected), name);
            require(family != probe::allocation_failure_family::throwing,
                    "throwing rejection recovered incorrectly");
            if (hits) { ++recovered; }
        }
        run(output);
        require(same(output, expected), "immediate retry full output");
        if (!hits) {
            std::printf("%s: %td ordinals, %zu throwing, %zu recovered\n",
                        name,
                        ordinal,
                        throwing,
                        recovered);
            return;
        }
    }
    require(false, "matrix did not reach no-hit ordinal");
}
struct flat_sink {
    n::path_set_builder64 builder;
    n::clipper_error_code diagnostic{n::clipper_error_code::ok};
    int fail_phase{};
    int cancels{};
    explicit flat_sink(n::path_set64& target)
        : builder(target) {}
    auto begin(const n::topology_layout64& layout) -> n::clipper_error_code {
        builder.begin(layout.ring_count, layout.point_count);
        if (fail_phase == 1) {
            diagnostic = n::clipper_error_code::resource_limit;
            throw std::runtime_error("begin");
        }
        if (fail_phase == 4) { return diagnostic = n::clipper_error_code::invalid_argument; }
        return n::clipper_error_code::ok;
    }
    auto acquire(const n::topology_ring_layout64& ring, std::span<geotypes::Point2i64>& out)
        -> n::clipper_error_code {
        out = builder.acquire(ring.point_count, geotypes::PathClosure::ClosedImplicit);
        if (fail_phase == 2) { return diagnostic = n::clipper_error_code::invalid_argument; }
        return n::clipper_error_code::ok;
    }
    auto finish() -> n::clipper_error_code {
        if (fail_phase == 3) { return diagnostic = n::clipper_error_code::coordinate_range; }
        builder.finish();
        return n::clipper_error_code::ok;
    }
    auto cancel() noexcept -> void {
        ++cancels;
        builder.cancel();
    }
};
}  // namespace
void vertex_growth_failure() {
    for (bool warm : {false, true}) {
        auto root = in::execution_resource_ledger{};
        in::VertexStorageList vertices;
        vertices.reserve(1);
        vertices.bind(&root);
        if (warm) {
            static_cast<void>(vertices.acquire(64));
            vertices.clear();
            root.reset_usage();
        }
        bool failed = false;
        {
            ScopedAllocationFailure reject{0};
            try { static_cast<void>(vertices.acquire(128)); }
            catch (const std::bad_alloc&) { failed = true; }
            require(failed && probe::failed_allocation_family() == probe::allocation_failure_family::throwing,
                    "actual Vertex resize throwing allocation rejected");
        }
        require(vertices.size() == 0 && root.staging_bytes == 0 && root.staging_high_water == 0 &&
                    root.reserved_staging_bytes == 0,
                "failed Vertex resize must not activate block/slots or raise actual peak");
        auto* data = vertices.acquire(128);
        data[0].pt = {1, 2}; data[127].pt = {3, 4};
        require(vertices.size() == 1 && data[0].pt == n::Point64(1, 2) && data[127].pt == n::Point64(3, 4),
                "Vertex resize retry constructs full usable range");
        require(root.staging_bytes == 128U * sizeof(in::Vertex) + sizeof(std::vector<in::Vertex>),
                "Vertex retry charges active record and constructed slots once");
        const auto exact = root.staging_bytes;
        vertices.clear();
        root.reset_usage();
        in::scoped_workspace_reservation held{&root, in::workspace_category::staging};
        held.set_bytes(1);
        root.maximum_staging_bytes = exact;
        bool denied = false;
        try { static_cast<void>(vertices.acquire(128)); }
        catch (const n::clipper_error& error) { denied = error.code() == n::clipper_error_code::resource_limit; }
        require(denied && vertices.size() == 0 && root.staging_bytes == 0 &&
                    root.staging_high_water == 0 && root.reserved_staging_bytes == 1,
                "retained Vertex activation preserves another owner's reservation and failed prefix");
        root.maximum_staging_bytes = exact + 1;
        {
            ScopedAllocationFailure reject{0};
            require(vertices.acquire(128) == data, "same-size Vertex activation reuses real retained objects");
        }
        require(vertices.size() == 1 && root.staging_bytes == exact && root.staging_high_water == exact &&
                    root.reserved_staging_bytes == 1,
                "retained Vertex activation has exact live/peak and unchanged held quota");
        vertices.clear();
        held.release();
        vertices.detach();
        require(root.staging_bytes == 0 && root.reserved_staging_bytes == 0, "Vertex cleanup releases logical owner");
    }
    std::puts("Vertex cold/warm resize rejection: active blocks/live/peak/quota zero; retries exact");
}
int main() {
    // Immutable publication covers both fresh outputs and retained live buffers.
    for (std::size_t count = 0; count != 5; ++count) {
        const auto source = n::Paths64(count, n::Path64{{1,2},{7,3},{4,9}});
        const auto immutable = source;
        for (int mode = 0; mode != 3; ++mode) {
            bool completed = false;
            for (std::ptrdiff_t ordinal = 0; ordinal != 16; ++ordinal) {
                n::Paths64 destination;
                if (mode) { destination.reserve(8); }
                if (mode == 2) {
                    destination.resize(6);
                    for (auto& path : destination) { path.reserve(32); path.push_back({91,71}); }
                }
                const auto old = destination;
                const auto* outer = destination.data();
                const auto outer_capacity = destination.capacity();
                const auto* inner = mode == 2 ? destination[0].data() : nullptr;
                bool failed = false;
                {
                    ScopedAllocationFailure failure{ordinal};
                    try { in::materialize_paths(source, destination); }
                    catch (const std::bad_alloc&) { failed = true; }
                }
                require(source == immutable, "immutable path copy source");
                require(destination == (failed ? old : source), "immutable path copy transaction");
                require(destination.capacity() >= outer_capacity, "immutable path copy outer capacity");
                if (mode) { require(destination.data() == outer, "immutable path copy retained outer"); }
                if (mode == 2 && !destination.empty()) {
                    require(destination[0].data() == inner, "immutable path copy retained inner");
                }
                if (!failed) { completed = true; break; }
            }
            require(completed, "immutable path copy fault ordinals terminate");
        }
    }

    // Switching output forms must reuse the same retained ring layout.
    {
        n::release_thread_caches();
        n::Paths64 subjects;
        for (std::int64_t i = 0; i < 64; ++i) {
            const auto x = i * 100;
            subjects.push_back({{x, 0}, {x + 40, 0}, {x + 40, 40}, {x, 40}});
        }
        n::borrowed_clip_request64 request;
        request.subjects = n::borrow_paths64(subjects);
        request.clip_type = n::ClipType::Union;
        request.fill_rule = n::FillRule::NonZero;
        n::path_set64 output;
        output.reserve(64, 256);
        flat_sink sink{output};
        require(n::clip_topology_checked(request, n::make_topology_writer64(sink)).has_value(),
                "prime the topology output layout");
        output.clear();
        in::execution_resource_ledger resources;
        in::scoped_workspace_charge charge{&resources, in::workspace_category::staging};
        auto error = n::clipper_error_code::internal_error;
        bool allocation_failed = false;
        {
            ScopedAllocationFailure failure{0};
            try { error = in::execute_borrowed_closed_paths(request, output, charge); }
            catch (const std::bad_alloc&) { allocation_failed = true; }
        }
        require(!allocation_failed && error == n::clipper_error_code::ok &&
                    probe::rejected_allocation_count() == 0,
                "output-form switch must reuse the retained ring layout without allocation");
        require(output.size() == 64 && output.point_count() == 256,
                "shared output layout preserves every ring and vertex");
    }


    vertex_growth_failure();
    require(probe::warm_rectangle_fast_paths_allocate_nothing(),
            "warm rectangle paths must not allocate temporary final paths");
    const n::Paths64 input{{{0, 0}, {100, 0}, {95, 100}, {0, 100}},
                           {{10, 10}, {80, 10}, {75, 80}, {10, 80}},
                           {{20, 20}, {60, 20}, {55, 60}, {20, 60}}};
    const n::Paths64 old{{{900, 900}, {950, 900}, {950, 950}, {900, 950}}};
    n::clip_request64 clip;
    clip.clip_type = n::ClipType::Union;
    clip.fill_rule = n::FillRule::EvenOdd;
    clip.subjects = input;
    n::paths64_result paths{old, old};
    matrix("clip", paths, [&](auto& out) { n::clip_into(clip, out); });
    const auto prepared = n::prepare_clip_request(clip);
    matrix("prepared clip", paths, [&](auto& out) { n::clip_into(prepared, out); });
    n::clip_tree64_result tree;
    static_cast<void>(tree.tree.add_child(old.front()));
    tree.open = old;
    tree.tree.set_scale(7.25);
    matrix("tree", tree, [&](auto& out) { n::clip_tree_into(clip, out); });
    n::clip_tree64_result moved_from;
    moved_from.tree.set_scale(9.5);
    auto moved_root_owner = std::move(moved_from.tree);
    require(moved_root_owner.scale() == 9.5, "moved fixture retains scale");
    require(n::polygon_tree_rebuild_access::size(moved_from.tree) == 0, "moved-from fixture");
    rootless_observers_reject(moved_from.tree);
    matrix("moved-from tree", moved_from, [&](auto& out) { n::clip_tree_into(clip, out); });
    n::clip_request64 empty_request;
    n::clip_tree_into(empty_request, moved_from);
    require(empty(moved_from.tree) && moved_from.tree.scale() == 9.5,
            "successful empty into restores root during preparation and preserves scale");
    n::PolyTree64 moved_again = std::move(moved_from.tree);
    require(moved_again.count() == 0, "moved owner remains traversable");
    moved_from.tree.clear();
    require(moved_from.tree.count() == 0 &&
                moved_from.tree.children(moved_from.tree.root()).empty() &&
                moved_from.tree.polygon(moved_from.tree.root()).empty(),
            "public clear restores moved-from root");
    n::offset_request64 offset;
    offset.paths = input;
    offset.delta = 2;
    matrix("offset", paths, [&](auto& out) { n::offset_into(offset, out); });
    n::offset_builder builder;
    builder.add(input).delta(2);
    matrix("builder paths", old, [&](auto& out) { builder.execute_into(out); }, true);
    matrix("builder tree", tree.tree, [&](auto& out) { builder.execute_into(out); }, true);
    n::rect_clip_request64 rect;
    rect.rect = {5, 5, 90, 90};
    rect.paths = input;
    n::rect_clip_result64 rect_old{old};
    matrix("rect", rect_old, [&](auto& out) { n::rect_clip_into(rect, out); });
    const auto prepared_rect = n::prepare_rect_clip_request(rect);
    matrix("prepared rect", rect_old, [&](auto& out) { n::rect_clip_into(prepared_rect, out); });
    const auto immutable_rect = n::prepare_immutable_rect_clip_paths(input);
    matrix("immutable rect", rect_old, [&](auto& out) {
        n::rect_clip_into(rect.rect, immutable_rect, out);
    });
    n::rect_clip_lines_request64 lines;
    lines.rect = rect.rect;
    lines.lines = {{{-10, 25}, {50, 25}, {150, 25}}};
    matrix("rect lines", rect_old, [&](auto& out) { n::rect_clip_lines_into(lines, out); });
    n::triangulation_request64 tri;
    tri.paths = {input.front()};
    n::triangulation_result64 tri_old;
    tri_old.triangles = old;
    tri_old.status = n::TriangulateResult::success;
    matrix("triangulation", tri_old, [&](auto& out) { n::triangulate_into(tri, out); });

    n::triangulation_requestd tri_d;
    tri_d.paths = {{{0.0, 0.0}, {2.0, 0.0}, {1.5, 2.0}, {0.0, 1.5}}};
    n::triangulation_resultd tri_d_old;
    tri_d_old.triangles = {{{90.0, 90.0}, {95.0, 90.0}, {90.0, 95.0}}};
    tri_d_old.status = n::TriangulateResult::success;
    matrix("triangulation double", tri_d_old, [&](auto& out) { n::triangulate_into(tri_d, out); });
    n::triangulation_request64 cached_tri;
    cached_tri.paths.resize(1);
    for (const auto point : input[0]) {
        for (int i = 0; i < 128; ++i) { cached_tri.paths[0].push_back(point); }
    }
    matrix("triangulation cache publication", tri_old, [&](auto& out) {
        n::triangulate_into(cached_tri, out);
    });
    matrix("fresh triangulation", n::triangulation_result64{}, [&](auto& out) {
        n::triangulate_into(tri, out);
    });
    matrix("fresh triangulation double", n::triangulation_resultd{}, [&](auto& out) {
        n::triangulate_into(tri_d, out);
    });
    matrix("fresh triangulation cache publication", n::triangulation_result64{}, [&](auto& out) {
        n::triangulate_into(cached_tri, out);
    });
    n::triangulation_requestd cached_tri_d;
    cached_tri_d.paths.resize(cached_tri.paths.size());
    for (std::size_t i = 0; i < cached_tri.paths.size(); ++i) {
        for (const auto& point : cached_tri.paths[i]) {
            cached_tri_d.paths[i].emplace_back(
                static_cast<double>(point.x), static_cast<double>(point.y));
        }
    }
    matrix("fresh double triangulation cache publication", n::triangulation_resultd{}, [&](auto& out) {
        n::triangulate_into(cached_tri_d, out);
    });

    // Target expands from 1 live path to 3; reject each inner reserve, including
    // the second and third, and verify the original cardinality and coordinates.
    for (std::ptrdiff_t ordinal = 0; ordinal < 3; ++ordinal) {
        auto target = old;
        target.reserve(3);
        bool failed = false;
        {
            ScopedAllocationFailure failure{ordinal};
            try {
                in::paths_rebuild<int64_t> tx{target};
                tx.prepare(3, [](auto) { return 100U; });
                tx.begin();
                tx.finish();
            } catch (const std::bad_alloc&) { failed = true; }
        }
        require(failed && target == old, "1->3 path capacity rollback");
    }
    struct node_layout {
        std::size_t point_count;
        std::size_t child_count;
    };
    const std::array<node_layout, 4> layout{{{0, 3}, {100, 0}, {100, 0}, {100, 0}}};
    for (std::ptrdiff_t ordinal = 0; ordinal < 4; ++ordinal) {
        auto target = tree.tree;
        target.reserve(4);
        bool failed = false;
        {
            ScopedAllocationFailure failure{ordinal};
            try {
                n::polygon_tree_rebuild_access::prepare(target, layout);
            } catch (const std::bad_alloc&) { failed = true; }
        }
        require(failed && same(target, tree.tree), "tree capacity rollback");
    }

    // Build actual engine-owned linked rings before observing materialization.
    in::engine_output_owner owner;
    auto& record = owner.create_outrec();
    std::array<in::output_point_node*, 4> points;
    for (std::size_t i = 0; i < 4; ++i) { points[i] = &owner.create_outpt(input[0][i], record); }
    for (std::size_t i = 0; i < 4; ++i) {
        points[i]->next = points[(i + 1) % 4];
        points[i]->prev = points[(i + 3) % 4];
    }
    record.pts = points[3];
    const std::array<n::topology_polygon_layout64, 1> polygons{
        {{n::topology_no_polygon_index, 1, 4}}};
    const std::array<in::closed_ring_descriptor64, 1> rings{
        {{&record, 0, n::topology_ring_role::shell, 4}}};
    n::path_set64 flat;
    flat.reserve(1, 4);
    flat_sink sink{flat};
    auto writer = n::make_topology_writer64(sink);
    n::topology_write_stats64 stats;
    n::clipper_error_code error;
    {
        ScopedAllocationFailure failure{0};
        error = in::write_topology(writer, {}, polygons, rings, 4, 0, stats);
    }
    require(error == n::clipper_error_code::ok && probe::rejected_allocation_count() == 0,
            "flat materialization must allocate zero");
    require(flat.size() == 1 && flat.point_count() == 4 &&
                flat.descriptors()[0].pointOffset == 0 && flat.descriptors()[0].pointCount == 4 &&
                flat.descriptors()[0].closure == geotypes::PathClosure::ClosedImplicit &&
                std::ranges::equal(flat[0], input[0]) && stats.output_ring_acquire_count == 1 &&
                stats.output_final_point_writes == 4,
            "flat geometry and actual materialization statistics");
    for (int phase = 1; phase <= 4; ++phase) {
        sink.fail_phase = phase;
        sink.cancels = 0;
        stats = {};
        error = in::write_topology(writer, {}, polygons, rings, 4, 0, stats);
        const auto expected_error = phase == 1
                                        ? n::clipper_error_code::sink_failure
                                        : (phase == 3 ? n::clipper_error_code::coordinate_range
                                                      : n::clipper_error_code::invalid_argument);
        require(
            error == expected_error && flat.empty() && flat.point_count() == 0 && sink.cancels == 1,
            "begin/acquire/finish failure cancels once and clears all fields");
        const auto diagnostic = sink.diagnostic;
        {
            ScopedAllocationFailure failure{0};
            sink.cancel();
            sink.cancel();
        }
        require(probe::rejected_allocation_count() == 0 && sink.diagnostic == diagnostic &&
                    flat.empty(),
                "idempotent allocation-free cancel preserves diagnostic");
    }

    sink.fail_phase = 0;
    sink.cancels = 0;
    flat.append(old[0], geotypes::PathClosure::ClosedImplicit);
    n::borrowed_clip_request64 borrowed;
    borrowed.clip_type = n::ClipType::Union;
    borrowed.subjects = n::borrow_paths64(input);
    borrowed.limits.maximum_output_point_count = 0;
    const auto limited = n::clip_topology_checked(borrowed, writer);
    require(!limited && limited.error() == n::clipper_error_code::resource_limit &&
                sink.cancels == 0 && flat.size() == 1 && std::ranges::equal(flat[0], old[0]),
            "admission failure must not begin or touch external output");
    auto clip_alias = clip;
    const auto clip_alias_expected = n::clip(clip_alias);
    in::execute_clip_paths_into_validated(clip_alias, clip_alias.subjects, clip_alias.open_subjects);
    require(clip_alias.subjects == clip_alias_expected.closed &&
                clip_alias.open_subjects == clip_alias_expected.open,
            "owning clip consumes aliased input before output changes");
    n::Paths64 alias = input;
    const auto alias_expected = probe::execute_rectclip(rect.rect, alias, probe::rectclip_mode::polygons);
    std::vector<n::Rect64> alias_bounds;
    require(in::build_rectclip_path_bounds(alias, false, alias_bounds), "raw bounds preparation");
    in::rectclip_operation_lease rect_operation;
    in::execute_rectclip_into(rect_operation.context(rect.rect), alias, alias_bounds, in::rectclip_mode::polygons, alias);
    require(alias == alias_expected, "ordinary owning rect input fully consumed before mutation");
    alias = input;
    const auto line_alias_expected = probe::execute_rectclip(rect.rect, alias, probe::rectclip_mode::lines);
    in::execute_rectclip_lines_into(rect.rect, alias, alias_bounds, alias);
    require(alias == line_alias_expected, "line rect input fully consumed before mutation");
    auto candidate = old;
    try {
        in::paths_rebuild<int64_t> tx{candidate};
        tx.prepare(1, [](auto) { return 4U; });
        tx.begin();
        candidate[0].push_back({1, 2});
        throw std::runtime_error("after begin");
    } catch (const std::runtime_error&) {}
    require(candidate.empty(), "failure after begin clears candidate");
    require(clip.subjects == input && offset.paths == input && rect.paths == input,
            "source values preserved");
    struct original_tree_layout {
        std::vector<n::Path64> nodes;
        double scale;
    };
    static_assert(sizeof(n::PolyTree64) == sizeof(original_tree_layout));
    static_assert(alignof(n::PolyTree64) == alignof(original_tree_layout));
    std::printf(
        "same-ABI tree: size %zu alignment %zu\n", sizeof(n::PolyTree64), alignof(n::PolyTree64));
    std::puts(
        "capacity rollback, flat phase allocation, actual stats and sink phase matrix passed");
}
