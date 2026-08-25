#pragma once

#include "clipper2next/triangulation.h"

namespace clipper2next::internal {

[[nodiscard]] auto execute_triangulation(const Paths64& paths,
                                         bool use_delaunay,
                                         TriangulateResult& result) -> Paths64;

// Frees this thread's triangulation cache and reusable execution storage.
auto release_triangulation_thread_state() noexcept -> void;
auto release_triangulation_execution_thread_state() noexcept -> void;

}  // namespace clipper2next::internal
