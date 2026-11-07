#pragma once
#include "triangulation/private/triangulation_context.h"
#include "triangulation/private/triangulation_boundary_geometry.h"

namespace clipper2next::internal {
[[nodiscard]] auto boundary_winding(std::span<const Point64> path,
                                    Point64 a,
                                    Point64 b,
                                    boundary_ray displacement) noexcept -> int64_t;
auto build_boundary_events(triangulation_context& context) -> void;
[[nodiscard]] auto event_for_ray(const triangulation_context& context,
                                 const triangulation_vertex* vertex,
                                 boundary_ray ray) noexcept -> std::size_t;
}  // namespace clipper2next::internal
