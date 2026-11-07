#include "clipper2next/clip.h"
#include "clipper2next/rectclip.h"
#include "clipper2next/offset.h"
#include "clipper2next/minkowski.h"
#include "clipper2next/triangulation.h"
#include "clipper2next/batch.h"
#include "clipper2next/api/memory.h"
#include "support/allocation_failure_probe.h"
#include "support/engine_execution_override.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <new>
#include <optional>
#include <tuple>
#include <span>
#include <vector>

namespace next = clipper2next;

namespace {

struct captured_polygon final {
    std::size_t parent{};
    std::size_t ring_count{};
    std::size_t point_count{};
    auto operator==(const captured_polygon&) const -> bool = default;
};
struct captured_ring final {
    std::size_t polygon_index{};
    next::topology_ring_role role{};
    next::Path64 points;
    auto operator==(const captured_ring&) const -> bool = default;
};
struct captured_topology final {
    std::vector<captured_polygon> polygons;
    std::vector<captured_ring> rings;
    std::size_t ring_count{};
    std::size_t point_count{};
    std::size_t maximum_ring_point_count{};
    std::size_t staging_workspace_bytes{};
    bool committed{};
    next::topology_write_stats64 stats{};
};
struct collecting_sink final {
    captured_topology output;
    auto begin(const next::topology_layout64& layout) -> next::clipper_error_code {
        output.ring_count = layout.ring_count;
        output.point_count = layout.point_count;
        output.maximum_ring_point_count = layout.maximum_ring_point_count;
        output.staging_workspace_bytes = layout.staging_workspace_bytes;
        for (const auto& polygon : layout.polygons) {
            output.polygons.push_back(
                {polygon.parent_polygon_index, polygon.ring_count, polygon.point_count});
        }
        return next::clipper_error_code::ok;
    }
    auto acquire(const next::topology_ring_layout64& ring, std::span<next::Point64>& destination)
        -> next::clipper_error_code {
        output.rings.push_back({ring.polygon_index, ring.role, next::Path64(ring.point_count)});
        destination = output.rings.back().points;
        return next::clipper_error_code::ok;
    }
    auto finish() -> next::clipper_error_code {
        output.committed = true;
        return next::clipper_error_code::ok;
    }
    auto cancel() noexcept -> void {
        output.rings.clear();
        output.polygons.clear();
        output.committed = false;
    }
};

template <typename Coordinate>
auto same_output(const next::Paths<Coordinate>& a, const next::Paths<Coordinate>& b) -> bool {
    return a == b;
}
auto same_output(const next::paths64_result& a, const next::paths64_result& b) -> bool {
    return a.closed == b.closed && a.open == b.open;
}
auto same_tree_node(const next::PolyTree64& a,
                    next::PolyTree64::node_id first,
                    const next::PolyTree64& b,
                    next::PolyTree64::node_id second) -> bool {
    if (a.polygon(first) != b.polygon(second) || a.depth(first) != b.depth(second) ||
        a.is_hole(first) != b.is_hole(second) || a.count(first) != b.count(second)) {
        return false;
    }
    for (std::size_t i = 0; i < a.count(first); ++i) {
        const auto ac = a.child(first, i);
        const auto bc = b.child(second, i);
        if (a.parent(ac) != first || b.parent(bc) != second || !same_tree_node(a, ac, b, bc)) {
            return false;
        }
    }
    return true;
}
auto same_output(const next::clip_tree64_result& a, const next::clip_tree64_result& b) -> bool {
    return a.open == b.open && a.tree.scale() == b.tree.scale() &&
           same_tree_node(a.tree, a.tree.root(), b.tree, b.tree.root());
}
auto same_output(const next::rect_clip_result64& a, const next::rect_clip_result64& b) -> bool {
    return a.paths == b.paths;
}
auto same_output(const next::triangulation_result64& a, const next::triangulation_result64& b)
    -> bool {
    return a.status == b.status && a.triangles == b.triangles;
}
auto same_output(const next::triangulation_resultd& a, const next::triangulation_resultd& b)
    -> bool {
    return a.status == b.status && a.triangles == b.triangles;
}
auto same_output(const captured_topology& a, const captured_topology& b) -> bool {
    const auto layout = [](const auto& value) {
        return std::tie(value.polygons,
                        value.rings,
                        value.ring_count,
                        value.point_count,
                        value.maximum_ring_point_count,
                        value.staging_workspace_bytes,
                        value.committed);
    };
    // Scratch growth can vary on an immediate retry; every other public
    // statistic describes stable input/output work or logical resource use.
    const auto logical_stats = [](const auto& stats) {
        return std::tie(stats.input_path_count,
                        stats.input_point_count,
                        stats.input_collection_point_writes,
                        stats.engine_input_point_writes,
                        stats.output_ring_acquire_count,
                        stats.output_final_point_writes,
                        stats.output_polygon_count,
                        stats.output_ring_count,
                        stats.output_point_count,
                        stats.peak_workspace_bytes,
                        stats.planned_engine_work,
                        stats.planned_engine_workspace_bytes);
    };
    return layout(a) == layout(b) && logical_stats(a.stats) == logical_stats(b.stats);
}
auto same_output(const next::borrowed_offset_stage_result64& a,
                 const next::borrowed_offset_stage_result64& b) -> bool {
    if (!std::ranges::equal(a.paths.points(), b.paths.points()) ||
        !std::ranges::equal(
            a.paths.descriptors(), b.paths.descriptors(), [](const auto& x, const auto& y) {
                return x.pointOffset == y.pointOffset && x.pointCount == y.pointCount &&
                       x.closure == y.closure && std::ranges::equal(x.reserved, y.reserved);
            })) {
        return false;
    }
    // Reallocation count describes retained scratch, and may differ on an
    // immediate retry. Compare every logical/input/output/budget statistic.
    const auto logical_stats = [](const auto& stats) {
        return std::tie(stats.input_path_count,
                        stats.input_point_count,
                        stats.input_collection_point_writes,
                        stats.engine_input_point_writes,
                        stats.output_path_count,
                        stats.output_point_count,
                        stats.peak_workspace_bytes,
                        stats.planned_engine_work,
                        stats.planned_engine_workspace_bytes,
                        stats.output_is_disjoint_simple_shells);
    };
    return logical_stats(a.stats) == logical_stats(b.stats);
}
auto same_output(const std::vector<next::paths64_result>& a,
                 const std::vector<next::paths64_result>& b) -> bool {
    return std::ranges::equal(a, b, [](const auto& x, const auto& y) { return same_output(x, y); });
}

auto valid(const next::paths64_result& value) -> bool {
    return !value.closed.empty() || !value.open.empty();
}
auto valid(const next::clip_tree64_result& value) -> bool {
    return value.tree.count() > 0;
}
auto valid(const next::rect_clip_result64& value) -> bool {
    return !value.paths.empty();
}
auto valid(const next::triangulation_result64& value) -> bool {
    return value.status == next::TriangulateResult::success && !value.triangles.empty();
}
auto valid(const next::triangulation_resultd& value) -> bool {
    return value.status == next::TriangulateResult::success && !value.triangles.empty();
}
auto valid(const next::Paths64& value) -> bool {
    return !value.empty();
}
auto valid(const next::PathsD& value) -> bool {
    return !value.empty();
}
auto valid(const captured_topology& value) -> bool {
    return value.committed && value.point_count > 0 && !value.rings.empty();
}
auto valid(const next::borrowed_offset_stage_result64& value) -> bool {
    return !value.paths.empty();
}
auto valid(const std::vector<next::paths64_result>& value) -> bool {
    for (const auto& item : value) {
        if (!valid(item)) { return false; }
    }
    return !value.empty();
}

// Negative controls alter successful, still-nonempty results. These exercise
// the same comparator used for a recovered nothrow allocation in the matrix.
template <typename Coordinate>
auto damage_output(next::Paths<Coordinate>& value) -> void {
    value.front().front().x += 1;
}
auto damage_output(next::paths64_result& value) -> void {
    damage_output(value.closed.empty() ? value.open : value.closed);
}
auto damage_output(next::rect_clip_result64& value) -> void {
    damage_output(value.paths);
}
auto damage_output(next::triangulation_result64& value) -> void {
    damage_output(value.triangles);
}
auto damage_output(next::triangulation_resultd& value) -> void {
    damage_output(value.triangles);
}
auto damage_output(next::borrowed_offset_stage_result64& value) -> void {
    value.paths.mutable_path(0).front().x += 1;
}
auto damage_output(captured_topology& value) -> void {
    value.rings.front().points.front().x += 1;
}
auto damage_output(std::vector<next::paths64_result>& value) -> void {
    damage_output(value.front());
}
auto copy_tree_with_damaged_leaf(const next::PolyTree64& source,
                                 next::PolyTree64::node_id parent,
                                 next::PolyTree64& target,
                                 next::PolyTree64::node_id destination,
                                 bool& damaged) -> void {
    for (const auto child : source.children(parent)) {
        auto polygon = source.polygon(child);
        if (!damaged && source.count(child) == 0) {
            polygon.front().x += 1;
            damaged = true;
        }
        const auto copied = target.add_child(destination, std::move(polygon));
        copy_tree_with_damaged_leaf(source, child, target, copied, damaged);
    }
}
auto damage_output(next::clip_tree64_result& value) -> void {
    next::PolyTree64 changed;
    changed.set_scale(value.tree.scale());
    bool damaged = false;
    copy_tree_with_damaged_leaf(value.tree, value.tree.root(), changed, changed.root(), damaged);
    value.tree = std::move(changed);
}

using failure_family = next::tests::allocation_failure_family;

auto probe_family_contract() -> bool {
    bool threw = false;
    {
        const next::tests::ScopedAllocationFailure failure{0};
        try {
            const auto pointer = ::operator new(8);
            ::operator delete(pointer);
        } catch (const std::bad_alloc&) { threw = true; }
        if (!threw || next::tests::failed_allocation_family() != failure_family::throwing ||
            next::tests::rejected_allocation_count() != 1) {
            return false;
        }
    }
    {
        const next::tests::ScopedAllocationFailure failure{0};
        const auto pointer = ::operator new(8, std::nothrow);
        const auto unexpectedly_allocated = pointer != nullptr;
        ::operator delete(pointer);
        if (unexpectedly_allocated ||
            next::tests::failed_allocation_family() != failure_family::nothrow ||
            next::tests::rejected_allocation_count() != 1) {
            return false;
        }
    }
    {
        const next::tests::ScopedAllocationFailure failure{1};
        const auto pointer = ::operator new(8);
        ::operator delete(pointer);
        if (next::tests::failed_allocation_family() != failure_family::none ||
            next::tests::rejected_allocation_count() != 0) {
            return false;
        }
    }
    std::puts(
        "probe family: throwing/nothrow actual rejection and next-scope no-hit reset verified");
    return true;
}

// Only a rejected nothrow request may have been recovered internally without
// an exception reaching checked_call. Even then every output must match.
template <typename Result>
auto accepted_outcome(failure_family family, const Result& result, const Result& baseline) -> bool {
    if (!result) {
        return family != failure_family::none &&
               result.error() == next::clipper_error_code::allocation_failure;
    }
    return family != failure_family::throwing && same_output(*result, *baseline);
}

template <typename Result>
auto return_observation_allocates_nothing(const Result& baseline) -> bool {
    auto staged = baseline;  // Snapshot copying is outside the rejection scope.
    std::optional<Result> observed;
    bool escaped = false;
    {
        const next::tests::ScopedAllocationFailure reject{0};
        try {
            // Both test-side transfers used after the checked API returns:
            // topology collector -> expected, then expected -> observation.
            Result transferred{std::move(*staged)};
            observed.emplace(std::move(transferred));
        } catch (...) { escaped = true; }
    }
    return !escaped && next::tests::rejected_allocation_count() == 0 && observed &&
           observed->has_value() && same_output(**observed, *baseline);
}

template <typename Operation>
auto allocation_matrix(const char* name, Operation operation) -> bool {
    next::release_thread_caches();
    const auto baseline = operation();
    if (!baseline || !valid(*baseline) || !return_observation_allocates_nothing(baseline)) {
        std::fprintf(stderr, "%s invalid baseline or allocating return observation\n", name);
        return false;
    }
    auto damaged = baseline;
    damage_output(*damaged);
    if (!accepted_outcome(failure_family::none, baseline, baseline) ||
        !accepted_outcome(failure_family::nothrow, baseline, baseline) ||
        accepted_outcome(failure_family::throwing, baseline, baseline) ||
        accepted_outcome(failure_family::nothrow, damaged, baseline)) {
        std::fprintf(stderr, "%s outcome oracle accepted wrong output/classification\n", name);
        return false;
    }
    std::size_t throwing_hits = 0, nothrow_hits = 0, recovered = 0;
    for (std::ptrdiff_t ordinal = 0; ordinal < 10000; ++ordinal) {
        next::release_thread_caches();
        bool escaped = false;
        failure_family family;
        std::size_t hits;
        std::optional<decltype(operation())> result;
        {
            const next::tests::ScopedAllocationFailure failure{ordinal};
            try {
                result.emplace(operation());
            } catch (...) { escaped = true; }
            family = next::tests::failed_allocation_family();
            hits = next::tests::rejected_allocation_count();
        }
        if (escaped || !result || hits != (family == failure_family::none ? 0U : 1U) ||
            !accepted_outcome(family, *result, baseline)) {
            std::fprintf(stderr,
                         "%s ordinal=%td family=%d hits=%zu escaped=%d success=%d output mismatch "
                         "or classification failure\n",
                         name,
                         ordinal,
                         static_cast<int>(family),
                         hits,
                         escaped,
                         result && result->has_value());
            return false;
        }
        if (family == failure_family::throwing) { ++throwing_hits; }
        if (family == failure_family::nothrow) {
            ++nothrow_hits;
            if (result->has_value()) {
                ++recovered;
                std::printf("%s recovered nothrow ordinal=%td exact-output=1\n", name, ordinal);
            }
        }
        // Compare the complete output on immediate retry, without resetting
        // scratch first. A nonempty partial result is not sufficient.
        const auto retry = operation();
        if (!retry || !same_output(*retry, *baseline)) {
            std::fprintf(stderr, "%s retry output mismatch\n", name);
            return false;
        }
        if (family == failure_family::none) {
            std::printf(
                "%s: ordinals [0,%td), throwing=%zu nothrow=%zu recovered=%zu first no-hit=%td\n",
                name,
                ordinal,
                throwing_hits,
                nothrow_hits,
                recovered,
                ordinal);
            return throwing_hits > 0;
        }
    }
    return false;
}

auto output_oracle_contract() -> bool {
    auto baseline = next::clipper_result<next::paths64_result>{std::in_place};
    baseline->closed = {{{0, 0}, {10, 0}, {0, 10}}, {{20, 0}, {30, 0}, {20, 10}}};
    auto partial = baseline;
    partial->closed.pop_back();
    if (!valid(*partial) || accepted_outcome(failure_family::nothrow, partial, baseline)) {
        return false;
    }

    next::clip_tree64_result nested;
    const auto shell = nested.tree.add_child(baseline->closed.front());
    const auto hole = nested.tree.add_child(shell, next::Path64{{1, 1}, {1, 4}, {4, 1}});
    (void)nested.tree.add_child(hole, next::Path64{{2, 2}, {3, 2}, {2, 3}});
    auto wrong_leaf = nested;
    damage_output(wrong_leaf);
    if (same_output(nested, wrong_leaf)) { return false; }
    next::clip_tree64_result wrong_parent;
    (void)wrong_parent.tree.add_child(baseline->closed.front());
    const auto detached = wrong_parent.tree.add_child(nested.tree.polygon(hole));
    (void)wrong_parent.tree.add_child(detached, nested.tree.polygon(nested.tree.child(hole, 0)));
    if (same_output(nested, wrong_parent)) { return false; }

    const captured_topology topology{
        .polygons = {{next::topology_no_polygon_index, 2, 6}, {0, 1, 3}},
        .rings = {{0, next::topology_ring_role::shell, baseline->closed.front()},
                  {0, next::topology_ring_role::hole, {{1, 1}, {1, 4}, {4, 1}}},
                  {1, next::topology_ring_role::shell, {{2, 2}, {3, 2}, {2, 3}}}},
        .ring_count = 3,
        .point_count = 9,
        .maximum_ring_point_count = 3,
        .staging_workspace_bytes = 128,
        .committed = true};
    auto changed = topology;
    changed.rings.pop_back();
    if (same_output(topology, changed)) { return false; }
    changed = topology;
    changed.rings.front().polygon_index = 1;
    if (same_output(topology, changed)) { return false; }
    changed = topology;
    changed.rings.front().role = next::topology_ring_role::hole;
    if (same_output(topology, changed)) { return false; }
    changed = topology;
    changed.polygons.back().parent = next::topology_no_polygon_index;
    if (same_output(topology, changed)) { return false; }
    changed = topology;
    ++changed.polygons.front().point_count;
    if (same_output(topology, changed)) { return false; }
    changed = topology;
    ++changed.staging_workspace_bytes;
    if (same_output(topology, changed)) { return false; }
    for (const auto field : {&next::topology_write_stats64::input_path_count,
                             &next::topology_write_stats64::input_point_count,
                             &next::topology_write_stats64::input_collection_point_writes,
                             &next::topology_write_stats64::engine_input_point_writes,
                             &next::topology_write_stats64::output_ring_acquire_count,
                             &next::topology_write_stats64::output_final_point_writes,
                             &next::topology_write_stats64::output_polygon_count,
                             &next::topology_write_stats64::output_ring_count,
                             &next::topology_write_stats64::output_point_count,
                             &next::topology_write_stats64::peak_workspace_bytes,
                             &next::topology_write_stats64::planned_engine_work,
                             &next::topology_write_stats64::planned_engine_workspace_bytes}) {
        changed = topology;
        ++(changed.stats.*field);
        if (same_output(topology, changed)) {
            std::fprintf(stderr,
                         "topology statistics-only corruption accepted with identical geometry\n");
            return false;
        }
    }
    changed = topology;
    ++changed.stats.staging_reallocation_count;
    if (!same_output(topology, changed)) { return false; }
    std::puts(
        "topology oracle: rejects all 12 stable statistics-only changes; excludes scratch growth "
        "count");
    std::puts(
        "output oracle: rejects nonempty partial paths, damaged descendant, wrong tree parent, "
        "missing ring, ring role/index and polygon/layout changes");
    return true;
}

auto request() -> next::clip_request64 {
    next::clip_request64 value;
    value.clip_type = next::ClipType::Union;
    value.fill_rule = next::FillRule::NonZero;
    value.subjects = {{{0, 0}, {40, 0}, {40, 40}, {0, 40}}};
    value.open_subjects = {{{-10, 20}, {60, 20}}};
    value.clips = {{{20, 10}, {50, 10}, {50, 50}, {20, 50}}};
    return value;
}

template <typename Operation>
auto engine_failure(const char* name, Operation operation) -> bool {
    next::tests::force_engine_failure(true);
    bool classified = false;
    try {
        const auto failed = operation();
        classified = !failed && failed.error() == next::clipper_error_code::internal_error;
    } catch (...) {}
    const auto calls = next::tests::engine_execution_count();
    next::tests::force_engine_failure(false);
    const auto retry = operation();
    const bool ok = classified && calls > 0 && retry.has_value() && valid(*retry);
    std::printf("%s engine false: calls=%zu classified=%d retry=%d\n",
                name,
                calls,
                classified,
                retry.has_value());
    return ok;
}

auto cache_request() -> next::clip_request64 {
    auto value = request();
    value.clip_type = next::ClipType::Intersection;
    value.subjects.clear();
    value.options.preserve_collinear = true;
    return value;
}

auto cache_failure_contract() -> bool {
    const auto raw = cache_request();
    next::tests::force_engine_failure(true);
    bool classified = false;
    try {
        (void)next::prepare_clip_request(raw);
    } catch (const next::clipper_error& error) {
        classified = error.code() == next::clipper_error_code::internal_error;
    }
    const auto build_calls = next::tests::engine_execution_count();
    next::tests::force_engine_failure(false);
    const auto cached = next::prepare_clip_request(raw);
    const auto expected = next::clip(cached);
    next::tests::force_engine_failure(true);
    const auto hit = next::clip_checked(cached);
    const auto hit_calls = next::tests::engine_execution_count();
    const auto oom_ok =
        allocation_matrix("cached materialization", [&] { return next::clip_checked(cached); });
    next::tests::force_engine_failure(false);
    std::printf("cache build false: calls=%zu classified=%d; cached hit calls=%zu\n",
                build_calls,
                classified,
                hit_calls);
    return classified && build_calls > 0 && hit && hit_calls == 0 && !expected.open.empty() &&
           hit->open == expected.open && oom_ok;
}

auto unchecked_engine_failure(const next::clip_request64& raw) -> bool {
    next::tests::force_engine_failure(true);
    bool classified = false;
    try {
        (void)next::clip(raw);
    } catch (const next::clipper_error& error) {
        classified = error.code() == next::clipper_error_code::internal_error;
    }
    next::tests::force_engine_failure(false);
    return classified && valid(next::clip(raw));
}

auto engine_success_cases() -> bool {
    auto no_clip = request();
    no_clip.clip_type = next::ClipType::NoClip;
    next::tests::force_engine_failure(true);
    const auto skipped = next::clip_checked(no_clip);
    const auto calls = next::tests::engine_execution_count();
    next::tests::force_engine_failure(false);
    auto no_scanline = request();
    no_scanline.subjects = {{{1, 1}}};
    no_scanline.open_subjects = {{{2, 2}}};
    no_scanline.clips.clear();
    const auto empty = next::clip_checked(no_scanline);
    return skipped && skipped->closed.empty() && skipped->open.empty() && calls == 0 && empty &&
           empty->closed.empty() && empty->open.empty() &&
           next::tests::engine_execution_count() == 1;
}

}  // namespace

int main() {
#if defined(_MSC_VER)
    std::printf("MSVC=%d iterator=%d CRT=%s static support, global new ordinal injection\n",
                _MSC_VER,
                _ITERATOR_DEBUG_LEVEL,
#if defined(_DEBUG)
                "/MDd"
#else
                "/MD"
#endif
    );
#else
    std::printf("Linux static support, global new ordinal injection\n");
#endif
    const auto raw = request();
    const auto prepared = next::prepare_clip_request(raw);
    const next::rect_clip_request64 rect{{10, 10, 30, 30}, raw.subjects};
    const next::triangulation_request64 tri{raw.subjects};
    const next::triangulation_requestd tri_d{{{{0, 0}, {40, 0}, {40, 40}, {0, 40}}}};
    const next::minkowski_request64 mink{raw.subjects.front(), raw.clips.front(), true};
    const next::minkowski_requestd mink_d{{{0, 0}, {2, 0}, {2, 2}}, {{0, 0}, {10, 0}}, false};
    next::offset_request64 offset;
    offset.paths = raw.subjects;
    offset.paths.push_back(raw.clips.front());
    offset.delta = 3;
    const std::vector<next::clip_request64> batch{raw, raw};
    const std::vector<next::prepared_clip_request64> prepared_batch{prepared, prepared};
    const next::borrowed_offset_group64 group{next::borrow_paths64(offset.paths)};
    const next::borrowed_offset_request64 borrowed_offset{.groups = std::span{&group, 1U},
                                                          .delta = 3};
    const next::rect_clip_lines_request64 lines{rect.rect, raw.open_subjects};
    const next::Paths64 topology_paths{{{0, 0}, {40, 0}, {40, 40}, {0, 40}},
                                       {{10, 10}, {10, 20}, {20, 20}, {20, 10}},
                                       {{100, 0}, {120, 0}, {120, 20}, {100, 20}}};
    auto capture_topology = [&](const next::Paths64& paths, next::FillRule fill)
        -> next::clipper_result<captured_topology> {
        next::borrowed_clip_request64 borrowed;
        borrowed.clip_type = raw.clip_type;
        borrowed.fill_rule = fill;
        borrowed.subjects = next::borrow_paths64(paths);
        collecting_sink sink;
        const auto result =
            next::clip_topology_checked(borrowed, next::make_topology_writer64(sink));
        if (!result) { return next::make_clipper_error<captured_topology>(result.error()); }
        sink.output.stats = *result;
        return std::move(sink.output);
    };
    auto topology = [&] { return capture_topology(topology_paths, raw.fill_rule); };
    const next::Paths64 splitting_paths{
        {{0, 0}, {4, 0}, {2, 0}, {4, 4}, {0, 4}},
        {{2, 1}, {6, 1}, {2, 5}, {2, 1}, {-2, 1}, {2, -3}},
    };
    auto splitting_topology = [&] {
        return capture_topology(splitting_paths, next::FillRule::EvenOdd);
    };
    bool ok = probe_family_contract() && output_oracle_contract();
    const auto topology_reference = topology();
    ok = topology_reference && topology_reference->polygons.size() == 2 &&
         topology_reference->rings.size() == 3 && topology_reference->stats.input_path_count == 3 &&
         topology_reference->stats.input_point_count == 12 &&
         topology_reference->stats.engine_input_point_writes == 12 &&
         topology_reference->stats.output_polygon_count == 2 &&
         topology_reference->stats.output_ring_count == 3 &&
         topology_reference->stats.output_ring_acquire_count == 3 &&
         topology_reference->stats.output_final_point_writes == 12 &&
         topology_reference->stats.output_point_count == 12 && ok;
    const auto split_reference = splitting_topology();
    ok = split_reference && split_reference->polygons.size() == 3 &&
         split_reference->rings.size() == 3 &&
         split_reference->stats.input_point_count == 11 &&
         split_reference->stats.output_ring_count == 3 && ok;
    ok = cache_failure_contract() && ok;
    ok = unchecked_engine_failure(raw) && ok;
    ok = engine_success_cases() && ok;
    ok = engine_failure("direct", [&] { return next::clip_checked(raw); }) && ok;
    ok = engine_failure("prepared", [&] { return next::clip_checked(prepared); }) && ok;
    ok = engine_failure("tree", [&] { return next::clip_tree_checked(raw); }) && ok;
    ok = engine_failure("offset cleanup", [&] { return next::offset_checked(offset); }) && ok;
    ok = engine_failure("borrowed topology", topology) && ok;
    ok = engine_failure("borrowed offset",
                        [&] { return next::offset_stage_checked(borrowed_offset); }) &&
         ok;
    ok = allocation_matrix("direct", [&] { return next::clip_checked(raw); }) && ok;
    ok = allocation_matrix("prepared", [&] { return next::clip_checked(prepared); }) && ok;
    ok = allocation_matrix("tree", [&] { return next::clip_tree_checked(raw); }) && ok;
    ok = allocation_matrix("rect", [&] { return next::rect_clip_checked(rect); }) && ok;
    ok =
        allocation_matrix("rect lines", [&] { return next::rect_clip_lines_checked(lines); }) && ok;
    ok = allocation_matrix("borrowed topology", topology) && ok;
    ok = allocation_matrix("topology split closure", splitting_topology) && ok;
    ok = allocation_matrix("tri64", [&] { return next::triangulate_checked(tri); }) && ok;
    ok = allocation_matrix("triD", [&] { return next::triangulate_checked(tri_d); }) && ok;
    ok = allocation_matrix("mink64", [&] { return next::minkowski_sum_checked(mink); }) && ok;
    ok = allocation_matrix("minkD", [&] { return next::minkowski_sum_checked(mink_d); }) && ok;
    ok = allocation_matrix("mink64 difference",
                           [&] { return next::minkowski_difference_checked(mink); }) &&
         ok;
    ok = allocation_matrix("minkD difference",
                           [&] { return next::minkowski_difference_checked(mink_d); }) &&
         ok;
    ok = allocation_matrix("offset", [&] { return next::offset_checked(offset); }) && ok;
    ok = allocation_matrix("borrowed offset",
                           [&] { return next::offset_stage_checked(borrowed_offset); }) &&
         ok;
    ok = allocation_matrix("batch", [&] { return next::clip_batch_checked(batch, {}); }) && ok;
    ok = allocation_matrix("prepared batch",
                           [&] { return next::clip_batch_checked(prepared_batch, {}); }) &&
         ok;
    return ok ? 0 : 1;
}
