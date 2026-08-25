#pragma once

#include "clipper2next/core/rect.h"

#include <vector>

namespace clipper2next::internal {

struct rectclip_context;

[[nodiscard]] auto acquire_reusable_rectclip_bounds_buffer() -> std::vector<Rect64>&;
[[nodiscard]] auto acquire_reusable_rectclip_context(const Rect64& rect) -> rectclip_context&;
auto release_rectclip_thread_state() noexcept -> void;

}  // namespace clipper2next::internal
