#pragma once

#include "clipper2next/core.h"

#include <span>

namespace clipper2next::internal {

// Exact sign for int64 points, including calls before coordinate admission.
// This does not admit those coordinates into the engine; public area stays approximate.
[[nodiscard]] auto signed_area_sign(std::span<const Point64> path) noexcept -> int;

}  // namespace clipper2next::internal
