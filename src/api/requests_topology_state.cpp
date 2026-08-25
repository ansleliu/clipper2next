#include "api/private/borrowed_topology_pipeline.h"

#include "clip/engine/private/engine_lifecycle.h"

namespace clipper2next::internal {

struct borrowed_topology_engine_state_slot final {
    clipper_base_state state{};
    borrowed_topology_workspace workspace{};
    bool in_use{};
    bool release_requested{};
};

namespace {

[[nodiscard]] auto state_storage() -> borrowed_topology_engine_state_slot& {
    thread_local borrowed_topology_engine_state_slot slot;
    return slot;
}

auto release_slot_storage(borrowed_topology_engine_state_slot& slot) noexcept -> void {
    release_engine_state_storage(slot.state);
    slot.workspace.release();
    slot.release_requested = false;
}

}  // namespace

auto borrowed_topology_workspace::clear() noexcept -> void {
    subjects.paths.clear();
    subjects.source_point_count = 0U;
    subjects.normalized_point_count = 0U;
    clips.paths.clear();
    clips.source_point_count = 0U;
    clips.normalized_point_count = 0U;
    polygon_layouts.clear();
    ring_descriptors.clear();
    record_metadata.clear();
    next_ring_by_polygon.clear();
}

auto borrowed_topology_workspace::release() noexcept -> void {
    clear();
    decltype(subjects.paths){}.swap(subjects.paths);
    decltype(clips.paths){}.swap(clips.paths);
    decltype(polygon_layouts){}.swap(polygon_layouts);
    decltype(ring_descriptors){}.swap(ring_descriptors);
    decltype(record_metadata){}.swap(record_metadata);
    decltype(next_ring_by_polygon){}.swap(next_ring_by_polygon);
}

borrowed_topology_engine_state_lease::borrowed_topology_engine_state_lease() noexcept {
    auto& slot = state_storage();
    if (slot.in_use) {
        state_ = &local_state_;
        workspace_ = &local_workspace_;
        return;
    }
    reset_engine_state_for_reuse(slot.state);
    slot.workspace.clear();
    slot.in_use = true;
    slot_ = &slot;
    state_ = &slot.state;
    workspace_ = &slot.workspace;
}

borrowed_topology_engine_state_lease::~borrowed_topology_engine_state_lease() {
    workspace_->clear();
    cleanup_engine_state(*state_);
    dispose_vertices_and_local_minima(*state_);
    if (slot_) {
        slot_->in_use = false;
        if (slot_->release_requested) { release_slot_storage(*slot_); }
    }
}

auto release_borrowed_topology_thread_state() noexcept -> void {
    auto& slot = state_storage();
    if (slot.in_use) {
        slot.release_requested = true;
        return;
    }
    release_slot_storage(slot);
}

}  // namespace clipper2next::internal
