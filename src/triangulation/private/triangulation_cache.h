#pragma once

#include "clipper2next/triangulation/request.h"

namespace clipper2next::internal {

[[nodiscard]] auto try_get_cached_triangulation(
    const triangulation_request64& request, triangulation_result64& result) -> bool;
auto store_cached_triangulation(
    const triangulation_request64& request, const triangulation_result64& result) -> void;
auto release_triangulation_cache() noexcept -> void;

}  // namespace clipper2next::internal
