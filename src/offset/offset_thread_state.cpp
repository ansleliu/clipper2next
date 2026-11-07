#include "offset/private/offset_thread_state.h"
#include "support/private/storage/thread_state_storage.h"

namespace clipper2next::internal {

struct offset_state_slot final {
    offset_state state{};
    bool in_use{};
    bool release_requested{};
};

namespace {
using storage = thread_state_storage<offset_state_slot>;

auto release_slot(offset_state_slot& slot) noexcept -> void {
    // Deallocation may reenter Offset or request another cache release.
    slot.in_use = true;
    slot.state.release();
    slot.release_requested = false;
    slot.in_use = false;
}
}  // namespace

offset_state_lease::offset_state_lease() {
    auto& slot = storage::acquire();
    if (slot.in_use) {
        state_ = &local_.emplace();
        return;
    }
    // Prior lease exit already restores the ready state.
    slot.in_use = true;
    slot_ = &slot;
    state_ = &slot.state;
}

offset_state_lease::~offset_state_lease() {
    state_->reset();
    if (slot_) {
        if (slot_->release_requested) {
            release_slot(*slot_);
        } else {
            slot_->in_use = false;
        }
    }
}

auto release_offset_thread_state() noexcept -> void {
    auto* slot = storage::peek_existing_slot();
    if (!slot) { return; }
    if (slot->in_use) { slot->release_requested = true; return; }
    release_slot(*slot);
}
}  // namespace clipper2next::internal
