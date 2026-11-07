#include "triangulation/private/triangulation_operation.h"
#include "support/private/storage/thread_state_storage.h"

namespace clipper2next::internal {

struct triangulation_operation_slot final {
    triangulation_operation_state state;
    bool in_use{};
    bool release_requested{};
};

namespace {
using storage = thread_state_storage<triangulation_operation_slot>;

auto release_slot(triangulation_operation_slot& slot) noexcept -> void {
    // Keep ownership through every payload's destruction/deallocation.
    slot.in_use = true;
    slot.state.release();
    slot.release_requested = false;
    slot.in_use = false;
}
}  // namespace

auto triangulation_operation_state::release() noexcept -> void {
    if (graph) { graph->release(); }
    if (result_cache) { result_cache->release(); }
}

triangulation_operation_lease::triangulation_operation_lease() {
    auto& slot = storage::acquire();
    if (slot.in_use) {
        state_ = &local_.emplace();
        return;
    }
    slot.in_use = true;
    slot_ = &slot;
    state_ = &slot.state;
}

triangulation_operation_lease::~triangulation_operation_lease() noexcept {
    if (!slot_) { return; }
    if (slot_->release_requested) {
        release_slot(*slot_);
    } else {
        // Preserve retained graph contents and the established cleanup timing.
        slot_->in_use = false;
    }
}

auto triangulation_operation_lease::context() -> triangulation_context& {
    if (!prepared_graph_) {
        if (state_->graph) {
            if (auto* result_cache = existing_cache()) { result_cache->materialize_graph_entry(); }
            state_->graph->clear();
        } else {
            state_->graph.emplace();
        }
        prepared_graph_ = &*state_->graph;
    }
    return *prepared_graph_;
}

auto triangulation_operation_lease::cache() -> triangulation_cache_state& {
    if (!state_->result_cache) { state_->result_cache.emplace(); }
    return *state_->result_cache;
}

auto release_triangulation_thread_state() noexcept -> void {
    auto* slot = storage::peek_existing_slot();
    if (!slot) { return; }
    if (slot->in_use) {
        slot->release_requested = true;
        return;
    }
    release_slot(*slot);
}

}  // namespace clipper2next::internal
