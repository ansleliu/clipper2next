#include "clip/private/owning_clip_state.h"
#include "clip/engine/private/engine_lifecycle.h"
#include "support/private/storage/thread_state_storage.h"

namespace clipper2next::internal {
struct owning_clip_state_slot final {
    clipper_base_state state{};
    bool in_use{};
    bool release_requested{};
};

using owning_clip_storage = thread_state_storage<owning_clip_state_slot>;

namespace {
auto release_slot(owning_clip_state_slot& slot) noexcept -> void {
    // Deallocation can reenter Clip or request another cache release.
    slot.in_use = true;
    release_engine_state_storage(slot.state);
    slot.release_requested = false;
    slot.in_use = false;
}
}  // namespace

owning_clip_state_lease::owning_clip_state_lease() {
    auto& slot = owning_clip_storage::acquire();
    if (slot.in_use) {
        state_ = &local_.emplace();
        return;
    }
    slot.in_use = true;
    reset_engine_state_for_reuse(slot.state);
    slot_ = &slot;
    state_ = &slot.state;
}
owning_clip_state_lease::~owning_clip_state_lease() {
    cleanup_engine_state(*state_);
    dispose_vertices_and_local_minima(*state_);
    if (slot_) {
        if (slot_->release_requested) {
            release_slot(*slot_);
        } else {
            slot_->in_use = false;
        }
    }
}
auto release_clip_thread_state() noexcept -> void {
    auto* slot = owning_clip_storage::peek_existing_slot();
    if (!slot) { return; }
    if (slot->in_use) { slot->release_requested = true; return; }
    release_slot(*slot);
}
}  // namespace clipper2next::internal
