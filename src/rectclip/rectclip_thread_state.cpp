#include "rectclip/private/rectclip_thread_state.h"
#include "support/private/storage/release_vector.h"
#include "support/private/storage/thread_state_storage.h"

#include "rectclip/private/rectclip_context.h"

namespace clipper2next::internal {

auto rectclip_context::reset_polygon_storage() -> void {
    op_container.clear();
    results.clear();
    for (auto& edge : edges) { edge.clear(); }
    start_locs.clear();
}

auto rectclip_context::reset_for_rect(const Rect64& rect_) -> void {
    rect = rect_;
    rect_as_path = {
        Point64{rect_.left, rect_.top},
        Point64{rect_.right, rect_.top},
        Point64{rect_.right, rect_.bottom},
        Point64{rect_.left, rect_.bottom},
    };
    rect_midpoint = rect_.midpoint();
    path_bounds = {};
    reset_polygon_storage();
}

auto rectclip_context::release() noexcept -> void {
    op_container.release();
    release_vector(results);
    for (auto& edge : edges) { release_vector(edge); }
    release_vector(start_locs);
}

auto rectclip_operation_state::reset() noexcept -> void {
    if (context) { context->reset_polygon_storage(); }
    bounds.clear();
}

auto rectclip_operation_state::release() noexcept -> void {
    if (context) { context->release(); }
    release_vector(bounds);
}

struct rectclip_slot final {
    rectclip_operation_state state{};
    bool in_use{};
    bool release_requested{};
};

namespace {
using storage = thread_state_storage<rectclip_slot>;

auto release_slot(rectclip_slot& slot) noexcept -> void {
    // Retain ownership through every allocator callback during deallocation.
    slot.in_use = true;
    slot.state.release();
    slot.release_requested = false;
    slot.in_use = false;
}
}  // namespace

rectclip_operation_lease::rectclip_operation_lease() {
    auto& slot = storage::acquire();
    if (slot.in_use) {
        state_ = &local_.emplace();
        return;
    }
    slot.in_use = true;
    slot_ = &slot;
    state_ = &slot.state;
}

rectclip_operation_lease::~rectclip_operation_lease() {
    state_->reset();
    if (slot_) {
        if (slot_->release_requested) { release_slot(*slot_); }
        else { slot_->in_use = false; }
    }
}

auto rectclip_operation_lease::context(const Rect64& rect) -> rectclip_context& {
    if (state_->context) { state_->context->reset_for_rect(rect); }
    else { state_->context.emplace(rect); }
    return *state_->context;
}

auto release_rectclip_thread_state() noexcept -> void {
    auto* slot = storage::peek_existing_slot();
    if (!slot) { return; }
    if (slot->in_use) { slot->release_requested = true; return; }
    release_slot(*slot);
}

}  // namespace clipper2next::internal
