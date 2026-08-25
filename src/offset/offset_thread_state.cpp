#include "offset/private/offset_thread_state.h"

namespace clipper2next::internal {
namespace {

struct reusable_offset_state_slot final {
    offset_state state{};
};

[[nodiscard]] auto reusable_offset_state_storage() -> reusable_offset_state_slot& {
    thread_local reusable_offset_state_slot slot;
    return slot;
}

}  // namespace

auto acquire_reusable_offset_state() -> offset_state& {
    auto& slot = reusable_offset_state_storage();
    slot.state.reset();
    return slot.state;
}

auto release_offset_thread_state() noexcept -> void {
    reusable_offset_state_storage().state.release();
}

}  // namespace clipper2next::internal
