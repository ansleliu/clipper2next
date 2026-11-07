// Same private owning boundary as the empty-phase contract; no production observer.
#include "clip/private/owning_clip_state.h"
#include "clip/private/clip_execution_strategy.h"
#include "clip/engine/private/engine_lifecycle.h"
#include "clipper2next/clip.h"
#include "clipper2next/api/memory.h"
#include "clipper2next/offset.h"
#include "clipper2next/rectclip.h"
#include "clipper2next/triangulation.h"
#include "clipper2next/core/path_set_builder.h"
#include "api/private/borrowed_topology_pipeline.h"
#include "clip/engine/private/engine_horizontal.h"
#include <array>
#include "offset/private/offset_thread_state.h"
#include "rectclip/private/rectclip_thread_state.h"
#include "rectclip/private/rectclip_context.h"
#include "triangulation/private/triangulation_context.h"
#include "triangulation/private/triangulation_cache.h"
#include "triangulation/private/triangulation_executor.h"
#include "triangulation/private/triangulation_operation.h"
#include "clip/engine/private/polygon_tree_rebuild_access.h"
#include "support/private/paths_rebuild.h"
#include "support/allocation_failure_probe.h"
#include "support/rectangle_fast_path_contract.h"
#include <thread>
#include <atomic>
#include <exception>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#if defined(_MSC_VER)
#include <crtdbg.h>
#include <windows.h>
#if defined(_DEBUG)
static_assert(_ITERATOR_DEBUG_LEVEL == 2);
#endif
#endif
namespace next = clipper2next;
namespace in = next::internal;
const char* active_phase = "startup";
void require(bool value) {
    if (!value) {
        std::fprintf(stderr, "cache release contract failed: %s; rejected allocations=%zu\n",
                     active_phase, next::tests::rejected_allocation_count());
        std::_Exit(91);
    }
}
void release_twice() {
    next::tests::ScopedAllocationFailure reject{0};
    next::release_thread_caches();
    next::release_thread_caches();
    require(next::tests::rejected_allocation_count() == 0);
}
void partial() {
    { auto lease = in::offset_state_lease{}; lease.state().normals.reserve(64); }
    release_twice();
    auto lease = in::offset_state_lease{};
    require(lease.state().normals.capacity() == 0);
}

namespace {
void* releasing_clip_minima = nullptr;
in::clipper_base_state* releasing_clip = nullptr;
bool clip_release_reentered = false;

void reenter_clip_release(void* pointer) noexcept {
    if (!releasing_clip_minima || pointer != releasing_clip_minima || clip_release_reentered) {
        return;
    }
    clip_release_reentered = true;
    in::owning_clip_state_lease nested;
    require(&nested.state() != releasing_clip);
    next::release_thread_caches();
}

void clip_release_reentry(bool deferred) {
    next::release_thread_caches();
    next::clip_request64 request;
    request.clip_type = next::ClipType::Union;
    request.subjects = {{{0, 0}, {100, 0}, {71, 88}}};
    request.open_subjects = {{{-5, -5}, {111, 7}}};
    const auto expected = next::clip_checked(request);
    require(expected.has_value() && !expected->closed.empty());

    auto outer = std::optional<in::owning_clip_state_lease>{std::in_place};
    releasing_clip = &outer->state();
    require(releasing_clip->minima_list_.capacity() != 0);
    releasing_clip_minima = releasing_clip->minima_list_.data();
    require(releasing_clip_minima != nullptr);
    if (!deferred) { outer.reset(); }
    clip_release_reentered = false;
    const auto previous = next::tests::set_deallocation_callback(&reenter_clip_release);
    next::release_thread_caches();
    if (deferred) {
        require(!clip_release_reentered);
        outer.reset();
    }
    next::tests::set_deallocation_callback(previous);
    releasing_clip_minima = nullptr;
    require(clip_release_reentered);
    {
        in::owning_clip_state_lease ready;
        require(&ready.state() == releasing_clip && ready.state().minima_list_.capacity() == 0);
    }
    const auto actual = next::clip_checked(request);
    require(actual.has_value() && actual->closed == expected->closed && actual->open == expected->open);
}

void* releasing_normals = nullptr;
in::offset_state* releasing_offset = nullptr;
bool offset_release_reentered = false;

void reenter_offset_release(void* pointer) noexcept {
    if (!releasing_normals || pointer != releasing_normals || offset_release_reentered) { return; }
    offset_release_reentered = true;
    try {
        in::offset_state_lease nested;
        require(&nested.state() != releasing_offset);
        nested.state().group_delta = 23;
        require(releasing_offset->group_delta == 0);
        next::offset_request64 request;
        request.delta = 1;
        const auto result = next::offset_checked(request);
        require(result.has_value() && result->closed.empty() && result->open.empty());
        next::release_thread_caches();
    } catch (...) { require(false); }
}

void offset_release_reentry(bool deferred) {
    next::release_thread_caches();
    auto outer = std::optional<in::offset_state_lease>{std::in_place};
    releasing_offset = &outer->state();
    releasing_offset->normals.reserve(64);
    releasing_offset->path_out.reserve(64);
    releasing_normals = releasing_offset->normals.data();
    if (!deferred) { outer.reset(); }
    offset_release_reentered = false;
    const auto previous = next::tests::set_deallocation_callback(&reenter_offset_release);
    next::release_thread_caches();
    if (deferred) { outer.reset(); }
    next::tests::set_deallocation_callback(previous);
    releasing_normals = nullptr;
    require(offset_release_reentered);
    in::offset_state_lease ready;
    require(&ready.state() == releasing_offset);
    require(ready.state().normals.capacity() == 0 && ready.state().path_out.capacity() == 0);
}
void* releasing_rect_results = nullptr;
in::rectclip_context* releasing_rect = nullptr;
std::vector<next::Rect64>* releasing_rect_bounds = nullptr;
bool rect_release_reentered = false;

void reenter_rect_release(void* pointer) noexcept {
    if (!releasing_rect_results || pointer != releasing_rect_results || rect_release_reentered) { return; }
    rect_release_reentered = true;
    try {
        in::rectclip_operation_lease nested;
        auto& context = nested.context({100, 100, 110, 110});
        require(&context != releasing_rect && &nested.bounds() != releasing_rect_bounds);
        nested.bounds().push_back({100, 100, 110, 110});
        require(releasing_rect->rect == next::Rect64{0, 0, 10, 10});
        next::rect_clip_request64 request;
        request.rect = {100, 100, 110, 110};
        request.paths = {{{102, 102}, {108, 102}, {108, 108}, {102, 108}}};
        require(next::rect_clip(request).paths == request.paths);
        next::release_thread_caches();
    } catch (...) { require(false); }
}

void rect_release_reentry(bool deferred) {
    next::release_thread_caches();
    auto outer = std::optional<in::rectclip_operation_lease>{std::in_place};
    releasing_rect = &outer->context({0, 0, 10, 10});
    releasing_rect_bounds = &outer->bounds();
    releasing_rect->results.reserve(64);
    releasing_rect->edges[0].reserve(32);
    releasing_rect_bounds->reserve(32);
    releasing_rect_results = releasing_rect->results.data();
    if (!deferred) { outer.reset(); }
    rect_release_reentered = false;
    const auto previous = next::tests::set_deallocation_callback(&reenter_rect_release);
    next::release_thread_caches();
    if (deferred) {
        require(!rect_release_reentered);
        require(releasing_rect->results.capacity() >= 64 && releasing_rect_bounds->capacity() >= 32);
        outer.reset();
    }
    next::tests::set_deallocation_callback(previous);
    releasing_rect_results = nullptr;
    require(rect_release_reentered);
    in::rectclip_operation_lease ready;
    require(&ready.context({0, 0, 10, 10}) == releasing_rect && &ready.bounds() == releasing_rect_bounds);
    require(releasing_rect->results.capacity() == 0 && releasing_rect->edges[0].capacity() == 0);
    require(releasing_rect_bounds->capacity() == 0);
}
void* releasing_topology_paths = nullptr;
in::borrowed_topology_workspace* releasing_topology = nullptr;
bool topology_release_reentered = false;

void reenter_topology_release(void* pointer) noexcept {
    if (pointer != releasing_topology_paths || topology_release_reentered) { return; }
    topology_release_reentered = true;
    try {
        in::borrowed_topology_engine_state_lease nested;
        require(&nested.workspace() != releasing_topology);
        next::release_thread_caches();
    } catch (...) { require(false); }
}

void topology_release_reentry(bool deferred) {
    next::release_thread_caches();
    auto outer = std::optional<in::borrowed_topology_engine_state_lease>{std::in_place};
    releasing_topology = &outer->workspace();
    releasing_topology->subjects.paths.reserve(32);
    releasing_topology_paths = releasing_topology->subjects.paths.data();
    if (!deferred) { outer.reset(); }
    topology_release_reentered = false;
    const auto previous = next::tests::set_deallocation_callback(&reenter_topology_release);
    next::release_thread_caches();
    if (deferred) {
        require(!topology_release_reentered);
        outer.reset();
    }
    next::tests::set_deallocation_callback(previous);
    releasing_topology_paths = nullptr;
    require(topology_release_reentered);
    in::borrowed_topology_engine_state_lease ready;
    require(&ready.workspace() == releasing_topology);
    require(ready.workspace().subjects.paths.capacity() == 0);
}
}  // namespace
void all_owners() {
    const next::Paths64 polygon{{{0, 0}, {100, 0}, {100, 100}, {0, 100}}};
    auto clip_request = next::clip_request64{};
    clip_request.subjects = polygon;
    clip_request.clip_type = next::ClipType::Union;
    require(!in::execute_clip_validated(clip_request).closed.empty());
    auto owning_lease = std::optional<in::owning_clip_state_lease>{std::in_place};
    auto& owning = owning_lease->state();
    in::add_paths_to_state(owning, polygon, next::PathType::Subject, false);
    owning.intersect_nodes_.reserve(32);
    owning.scanbeam_top_edges_.reserve(32);
    owning.scanbeam_unit_edges_.reserve(32);
    owning.scanbeam_unit_scratch_.reserve(32);
    owning.horz_seg_list_.reserve(32);
    owning.horz_join_list_.reserve(32);
    auto topology = std::optional<in::borrowed_topology_engine_state_lease>{std::in_place};
    auto* top_state = &topology->prepare_engine();
    in::add_paths_to_state(*top_state, polygon, next::PathType::Subject, false);
    topology->workspace().subjects.paths.resize(32);
    auto* workspace = &topology->workspace();
    auto offset = std::optional<in::offset_state_lease>{std::in_place};
    auto* offset_state = &offset->state();
    offset_state->normals.reserve(64);
    offset_state->path_out.reserve(64);
    offset_state->callback_path.reserve(64);
    auto rect_operation = std::optional<in::rectclip_operation_lease>{std::in_place};
    auto& rect = rect_operation->context({0, 0, 10, 10});
    static_cast<void>(rect.op_container.emplace());
    rect.results.reserve(32);
    for (auto& edge : rect.edges) { edge.reserve(32); }
    rect.start_locs.reserve(32);
    auto& bounds = rect_operation->bounds();
    bounds.reserve(32);
    next::triangulation_request64 tri_request;
    tri_request.paths = polygon;
    const auto triangles = next::triangulate(tri_request);
    require(triangles.status == next::TriangulateResult::success);
    // Populate the real result cache using admitted cache-sized input.
    tri_request.paths.clear();
    for (int64_t i = 0; i != 128; ++i) {
        const auto x = i * 200;
        tri_request.paths.push_back({{x, 0}, {x + 100, 0}, {x + 100, 100}, {x, 100}});
    }
    auto tri_operation = std::optional<in::triangulation_operation_lease>{std::in_place};
    auto tri_status = next::TriangulateResult::fail;
    const auto& cache_graph =
        in::execute_triangulation_graph(*tri_operation, tri_request.paths, true, tri_status);
    require(tri_status == next::TriangulateResult::success);
    require(cache_graph.size() == 256);
    in::store_cached_triangulation_graph(*tri_operation, tri_request, tri_status, cache_graph);
    require(tri_operation->cache().entries.size() == 1);
    in::triangulation_context tri;
    static_cast<void>(in::create_triangulation_vertex(tri, {1, 2}));
    require(tri.source_paths.append(polygon));
    tri.boundary_events.reserve(16);
    tri.local_minima.reserve(16);
    tri.edges.reserve(16);
    tri.pending_delaunay.reserve(16);
    next::path_set64 target;
    auto builder = std::optional<next::path_set_builder64>{std::in_place, target};
    builder->begin(1, 4);
    owning_lease->state().intersect_nodes_.reserve(32);
    auto nested_offset = std::optional<in::offset_state_lease>{std::in_place};
    nested_offset->state().normals.reserve(32);
    require(&nested_offset->state() != offset_state);
    auto nested_topology = std::optional<in::borrowed_topology_engine_state_lease>{std::in_place};
    nested_topology->workspace().subjects.paths.resize(32);
    require(&nested_topology->state() != top_state);
    auto nested_owning = std::optional<in::owning_clip_state_lease>{std::in_place};
    nested_owning->state().intersect_nodes_.reserve(32);
    require(&nested_owning->state() != &owning_lease->state());
    struct original_tree_layout { std::vector<next::Path64> nodes; double scale; };
    static_assert(sizeof(next::PolyTree64) == sizeof(original_tree_layout));
    static_assert(alignof(next::PolyTree64) == alignof(original_tree_layout));
    next::PolyTree64 candidate_tree;
    static_cast<void>(candidate_tree.add_child(polygon.front()));
    const auto* root_children = candidate_tree.children(candidate_tree.root()).data();
    next::PolyTree64 moved_from_tree;
    auto moved_root_owner = std::move(moved_from_tree);
    require(moved_root_owner.count() == 0);
    next::Paths64 candidate_paths = polygon;
    {
        next::tests::ScopedAllocationFailure reject{0};
        next::polygon_tree_rebuild_access::cancel(moved_from_tree);
        require(next::polygon_tree_rebuild_access::size(moved_from_tree) == 0);
        next::polygon_tree_rebuild_access::cancel(candidate_tree);
        next::polygon_tree_rebuild_access::cancel(candidate_tree);
        require(candidate_tree.children(candidate_tree.root()).data() == root_children);
        require(next::polygon_tree_rebuild_access::size(candidate_tree) == 1);
        {
            in::paths_rebuild<int64_t> transaction{candidate_paths};
            transaction.begin();
        }
        require(candidate_paths.empty());
        next::release_thread_caches();
        next::release_thread_caches();
        require(cache_graph.size() == 256);
        require(rect.op_container.retained_capacity() != 0 && rect.results.capacity() >= 32);
        require(bounds.capacity() >= 32);
        require(offset_state->normals.capacity() >= 64);
        require(workspace->subjects.paths.size() == 32);
        nested_offset.reset();
        nested_topology.reset();
        nested_owning.reset();
        offset.reset();
        topology.reset();
        owning_lease.reset();
        tri_operation.reset();
        rect_operation.reset();
        builder->cancel();
        builder.reset();
        tri.release();
        tri.release();
        next::release_thread_caches();
        require(next::tests::rejected_allocation_count() == 0);
        require(offset_state->normals.capacity() == 0);
        require(offset_state->path_out.capacity() == 0);
        require(offset_state->callback_path.capacity() == 0);
        require(workspace->subjects.paths.capacity() == 0);
        require(owning.active_pool_.retained_capacity() == 0);
        require(owning.intersect_nodes_.capacity() == 0);
        require(owning.scanbeam_top_edges_.capacity() == 0);
        require(rect.op_container.retained_capacity() == 0);
        require(rect.results.capacity() == 0 && bounds.capacity() == 0);
        require(!tri.source_paths.retains_storage() && tri.boundary_events.capacity() == 0);
        require(tri.vertex_pool.retained_capacity() == 0 && tri.vertices.capacity() == 0);
        require(tri.local_minima.capacity() == 0 && tri.edges.capacity() == 0);
    }
}
auto release_executor(void* data, std::size_t count, std::size_t, std::size_t,
                      next::bulk_task_ref task) noexcept -> next::bulk_execution_error {
    ++*static_cast<std::size_t*>(data);
    release_twice();
    for (std::size_t i = 0; i < count; ++i) { task(i, i + 1); }
    return next::bulk_execution_error::none;
}
void active_executor() {
    next::Paths64 dense;
    for (std::int64_t i = 0; i < 512; ++i) {
        next::Path64 path;
        const auto x = i * 2000;
        for (std::int64_t j = 0; j < 256; ++j) { path.push_back({x + j, 0}); }
        for (std::int64_t j = 0; j < 256; ++j) { path.push_back({x + 256, j}); }
        for (std::int64_t j = 256; j > 0; --j) { path.push_back({x + j, 256}); }
        for (std::int64_t j = 256; j > 0; --j) { path.push_back({x, j}); }
        dense.push_back(std::move(path));
    }
    const next::Paths64 square{{{2000000, 0}, {2000100, 0}, {2000100, 100}, {2000000, 100}}};
    const next::borrowed_offset_group64 groups[]{
        {next::borrow_paths64(dense), next::JoinType::Miter, next::EndType::Polygon},
        {next::borrow_paths64(square), next::JoinType::Miter, next::EndType::Polygon}};
    const auto request = next::borrowed_offset_request64{.groups = groups, .delta = 5};
    in::offset_state* cached;
    { auto lease = in::offset_state_lease{}; cached = &lease.state(); }
    std::size_t calls = 0;
    const auto result = next::offset_stage_checked(request, {&calls, 16, &release_executor});
    require(result.has_value() && calls > 0);
    require(cached->normals.capacity() == 0 && cached->path_out.capacity() == 0);
    require(result->paths.size() == 513);
    std::int64_t left = 3000000, right = 0;
    for (auto point : result->paths[result->paths.size() - 1]) {
        left = (std::min)(left, point.x); right = (std::max)(right, point.x);
    }
    require(left == 1999995 && right == 2000105);
}
void run_on_fresh_thread(void (*operation)()) {
    std::atomic<bool> start{false}, finished{false};
    std::thread worker([&] {
        while (!start.load(std::memory_order_acquire)) {}
        operation();
        finished.store(true, std::memory_order_release);
    });
    // Constructor and startup have finished before rejection can start; join
    // waits until rejection is disabled. The parent only reads an atomic meanwhile.
    start.store(true, std::memory_order_release);
    while (!finished.load(std::memory_order_acquire)) {}
    worker.join();
}
void stable_order_without_sort_storage() {
    std::array<in::Vertex, 256> vertices{};
    in::LocalMinimaList minima;
    in::engine_output_owner owner;
    auto& record = owner.create_outrec();
    in::HorzSegmentList segments;
    for (std::size_t i = 0; i < vertices.size(); ++i) {
        vertices[i].pt = {static_cast<std::int64_t>(i % 5), static_cast<std::int64_t>(i % 3)};
        minima.emplace_back(vertices[i], i % 2 ? next::PathType::Clip : next::PathType::Subject, i % 3 == 0);
        auto& left = owner.create_outpt({static_cast<std::int64_t>(i % 5), 0}, record);
        auto& right = owner.create_outpt({100, 0}, record);
        auto& segment = segments.emplace_back(left);
        if (i % 7 != 0) { segment.set_right_point(right); }
    }
    for (int round = 0; round < 3; ++round) {
        std::reverse(minima.begin(), minima.end());
        std::reverse(segments.begin(), segments.end());
        auto expected_minima = minima;
        auto expected_segments = segments;
        std::stable_sort(expected_minima.begin(), expected_minima.end(), in::compare_local_minima_bottom_up);
        std::stable_sort(expected_segments.begin(), expected_segments.end(), in::compare_horizontal_segments);
        {
            next::tests::ScopedAllocationFailure reject{0};
            in::sort_local_minima(minima);
            in::sort_horizontal_segments(segments);
            require(next::tests::rejected_allocation_count() == 0);
        }
        for (std::size_t i = 0; i < minima.size(); ++i) {
            require(&minima[i].vertex.get() == &expected_minima[i].vertex.get());
            require(minima[i].polytype == expected_minima[i].polytype && minima[i].is_open == expected_minima[i].is_open);
            require(&segments[i].left_point() == &expected_segments[i].left_point());
            require(segments[i].has_right_point() == expected_segments[i].has_right_point());
        }
    }
}
void ledger_without_allocations() {
    next::tests::ScopedAllocationFailure reject{0};
    auto root = in::execution_resource_ledger{};
    root.maximum_workspace_bytes = 64;
    root.maximum_staging_bytes = 64;
    {
        auto first = in::scoped_workspace_charge{&root};
        require(first.try_set_slots(4, 16) == next::clipper_error_code::ok);
        auto second = std::move(first);
        require(second.try_reclassify(in::workspace_category::staging) == next::clipper_error_code::ok);
        require(second.try_set_slots(5, 16) == next::clipper_error_code::resource_limit);
        require(root.engine_bytes == 0 && root.staging_bytes == 64);
    }
    require(root.engine_bytes == 0 && root.staging_bytes == 0);
    root.reset_usage();
    {
        auto quota = in::scoped_workspace_reservation{&root};
        quota.set_bytes(64);
        require(root.engine_bytes == 0 && root.engine_high_water == 0 && root.reserved_engine_bytes == 64);
        quota.release();
        std::array<in::parallel_engine_workspace_slot, 1> observer_slots{};
        auto observer = in::parallel_engine_workspace_observer{std::span{observer_slots}};
        root.parallel_observer = &observer_slots[0];
        {
            auto child_owner = in::scoped_workspace_charge{&root};
            require(child_owner.try_set_slots(2, 16) == next::clipper_error_code::ok);
            require(observer.live() == 32 && observer.peak() == 32);
        }
        root.parallel_observer = nullptr;
        require(observer.live() == 0);
    }
    require(root.reserved_engine_bytes == 0);
    root.reset_usage();
    {
        auto owner = in::scoped_workspace_charge{&root, in::workspace_category::staging};
        {
            auto growth = in::scoped_workspace_growth{owner, 4U, 16U};
            require(root.staging_bytes == 0 && root.staging_high_water == 0 && root.reserved_staging_bytes == 64);
            growth.commit_slots(1U, 16U);
            require(root.staging_bytes == 16 && root.staging_high_water == 16 && root.reserved_staging_bytes == 48);
        }
        require(root.reserved_staging_bytes == 0 && root.staging_bytes == 16);
        auto denied = in::scoped_workspace_growth{&owner};
        require(denied.try_reserve_slots(5U, 16U) == next::clipper_error_code::resource_limit);
        require(root.reserved_staging_bytes == 0 && root.staging_high_water == 16);
    }
    require(root.staging_bytes == 0 && root.reserved_staging_bytes == 0);
    require(next::tests::rejected_allocation_count() == 0);
}
auto publish_exclusive_raw_paths_without_allocations() -> bool {
    namespace n = clipper2next;
    n::Paths64 source{{{80, 80}, {150, 68}, {192, 118}, {154, 178}, {90, 166}, {56, 108}}};
    const auto expected = source;
    bool completed = false;
    n::Paths64 actual;
    std::size_t rejected{};
    {
        n::tests::ScopedAllocationFailure failure{0};
        try { n::internal::materialize_paths(std::move(source), actual); completed = true; }
        catch (const std::bad_alloc&) {}
        rejected = n::tests::rejected_allocation_count();
    }
    return completed && rejected == 0 && actual == expected;
}

auto prepare_source_paths_without_transient_storage() -> bool {
    next::Paths64 input;
    for (int64_t i = 0; i != 129; ++i) {
        input.push_back({{i * 10, 0}, {i * 10 + 5, 0}, {i * 10 + 5, 5}, {i * 10, 5}});
    }
    in::triangulation_source_paths retained;
    if (!retained.append(input)) { return false; }
    retained.clear();
    bool complete = false;
    std::size_t rejected = 0;
    {
        next::tests::ScopedAllocationFailure failure{0};
        try { complete = retained.append(input); }
        catch (const std::bad_alloc&) {}
        rejected = next::tests::rejected_allocation_count();
    }
    if (!complete || rejected) { return false; }
    in::triangulation_source_paths fresh;
    complete = false;
    {
        next::tests::ScopedAllocationFailure failure{2};
        try { complete = fresh.append(input); }
        catch (const std::bad_alloc&) {}
        rejected = next::tests::rejected_allocation_count();
    }
    if (!complete || rejected || fresh.size() != input.size() || fresh.point_count() != 516) { return false; }
    for (std::size_t i = 0; i < input.size(); ++i) {
        const auto path = fresh[i];
        if (!std::equal(path.begin(), path.end(), input[i].begin(), input[i].end())) { return false; }
    }
    return true;
}

int main(int argc, char** argv) {
    if (!prepare_source_paths_without_transient_storage()) {
        std::fputs("FAIL: source preparation allocates transient contour storage\n", stderr);
        return 1;
    }
    if (!publish_exclusive_raw_paths_without_allocations()) {
        std::fputs("FAIL: exclusive raw path publication allocates\n", stderr);
        return 1;
    }

#if defined(_MSC_VER)
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _set_error_mode(_OUT_TO_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif
    std::set_terminate([] {
        std::fprintf(stderr, "cache release terminated: %s; rejected allocations=%zu\n",
                     active_phase, next::tests::rejected_allocation_count());
        std::_Exit(90);
    });
    active_phase = "stable ordering with no hidden sort buffer";
    stable_order_without_sort_storage();
    active_phase = "ledger transfer and destruction";
    ledger_without_allocations();
    active_phase = "warm rectangle materialization";
    require(next::tests::warm_rectangle_fast_paths_allocate_nothing());
    active_phase = "idle owning Clip release reentry";
    clip_release_reentry(false);
    active_phase = "deferred owning Clip release reentry";
    clip_release_reentry(true);
    active_phase = "idle Offset release reentry";
    offset_release_reentry(false);
    active_phase = "deferred Offset release reentry";
    offset_release_reentry(true);
    active_phase = "idle RectClip release reentry";
    rect_release_reentry(false);
    active_phase = "deferred RectClip release reentry";
    rect_release_reentry(true);
    active_phase = "idle topology release reentry";
    topology_release_reentry(false);
    active_phase = "deferred topology release reentry";
    topology_release_reentry(true);
    // Thread startup, synchronization and joining are outside rejection.
    if (argc == 1 || std::strcmp(argv[1], "fresh") == 0) { active_phase = "fresh"; run_on_fresh_thread(release_twice); }
    if (argc == 1 || std::strcmp(argv[1], "partial") == 0) { active_phase = "partial"; run_on_fresh_thread(partial); }
    if (argc == 1 || std::strcmp(argv[1], "all") == 0) { active_phase = "all owners and nested destruction"; all_owners(); }
    if (argc == 1 || std::strcmp(argv[1], "active") == 0) { active_phase = "active executor"; active_executor(); }
    std::puts("fresh/partial/all-active cache release: no allocations; retained storage released");
}
