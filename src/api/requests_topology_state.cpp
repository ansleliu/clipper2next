#include "api/private/borrowed_topology_pipeline.h"
#include "support/private/storage/release_vector.h"
#include "support/private/storage/thread_state_storage.h"

#include "clip/engine/private/engine_lifecycle.h"

namespace clipper2next::internal {

struct borrowed_topology_engine_state_slot final {
    clipper_base_state state{};
    borrowed_topology_workspace workspace{};
    bool in_use{};
    bool release_requested{};
};

namespace {

using storage = thread_state_storage<borrowed_topology_engine_state_slot>;

auto release_slot_storage(borrowed_topology_engine_state_slot& slot) noexcept -> void {
    slot.in_use = true;
    release_engine_state_storage(slot.state);
    slot.workspace.release();
    slot.release_requested = false;
    slot.in_use = false;
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
    closed_layout.clear();
    record_metadata.clear();
    next_ring_by_polygon.clear();
}

auto borrowed_topology_workspace::release() noexcept -> void {
    clear();
    release_vector(subjects.paths);
    release_vector(clips.paths);
    release_vector(polygon_layouts);
    release_vector(closed_layout);
    release_vector(record_metadata);
    release_vector(next_ring_by_polygon);
}

borrowed_topology_engine_state_lease::borrowed_topology_engine_state_lease(execution_resource_ledger* resources)
    : resources_(resources) {
    auto& slot = storage::acquire();
    if (slot.in_use) {
        state_ = &local_state_.emplace();
        workspace_ = &local_workspace_.emplace();
        if (resources_) { bind_workspace_resources(); }
        return;
    }
    slot.workspace.clear();
    slot.in_use = true;
    slot_ = &slot;
    state_ = &slot.state;
    workspace_ = &slot.workspace;
    if (resources_) { bind_workspace_resources(); }
}

auto borrowed_topology_engine_state_lease::prepare_engine() noexcept -> clipper_base_state& {
    reset_engine_state_for_reuse(*state_);
    engine_prepared_ = true;
    if (resources_) { bind_engine_resources(); }
    return *state_;
}

borrowed_topology_engine_state_lease::~borrowed_topology_engine_state_lease() {
    workspace_->clear();
    if (engine_prepared_) {
        cleanup_engine_state(*state_);
        dispose_vertices_and_local_minima(*state_);
    }
    if (resources_) { detach_resources(); }
    if (slot_) {
        slot_->in_use = false;
        if (slot_->release_requested) { release_slot_storage(*slot_); }
    }
}

auto borrowed_topology_engine_state_lease::bind_workspace_resources() -> void {
    workspace_->subjects.paths.bind(resources_);
    workspace_->clips.paths.bind(resources_);
    workspace_->polygon_layouts.bind(resources_);
    workspace_->closed_layout.bind(resources_);
    workspace_->record_metadata.bind(resources_);
    workspace_->next_ring_by_polygon.bind(resources_);
}

auto borrowed_topology_engine_state_lease::bind_engine_resources() -> void {
    state_->vertex_lists_.bind(resources_);
    state_->active_pool_.bind(resources_);
    state_->minima_list_.bind(resources_);
    state_->scanline_list_.bind(resources_);
    state_->intersect_nodes_.bind(resources_);
    state_->scanbeam_top_edges_.bind(resources_);
    state_->scanbeam_unit_edges_.bind(resources_);
    state_->scanbeam_unit_scratch_.bind(resources_);
    state_->horz_seg_list_.bind(resources_);
    state_->horz_join_list_.bind(resources_);
    state_->output_owner_.bind(resources_);
}

auto borrowed_topology_engine_state_lease::detach_resources() noexcept -> void {
    state_->vertex_lists_.detach(); state_->active_pool_.detach();
    state_->minima_list_.detach(); state_->scanline_list_.detach();
    state_->intersect_nodes_.detach(); state_->scanbeam_top_edges_.detach();
    state_->scanbeam_unit_edges_.detach(); state_->scanbeam_unit_scratch_.detach();
    state_->horz_seg_list_.detach(); state_->horz_join_list_.detach();
    state_->output_owner_.detach();
    workspace_->subjects.paths.detach(); workspace_->clips.paths.detach();
    workspace_->polygon_layouts.detach(); workspace_->closed_layout.detach();
    workspace_->record_metadata.detach(); workspace_->next_ring_by_polygon.detach();
    resources_ = nullptr;
}

auto release_borrowed_topology_thread_state() noexcept -> void {
    auto* existing = storage::peek_existing_slot();
    if (!existing) { return; }
    auto& slot = *existing;
    if (slot.in_use) {
        slot.release_requested = true;
        return;
    }
    release_slot_storage(slot);
}

}  // namespace clipper2next::internal
