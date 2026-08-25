#pragma once

#include "offset/private/offset_state.h"

namespace clipper2next::internal {

[[nodiscard]] auto acquire_reusable_offset_state() -> offset_state&;
auto release_offset_thread_state() noexcept -> void;

}  // namespace clipper2next::internal
