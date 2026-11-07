#pragma once
#include "clipper2next/core.h"
#include "triangulation/private/triangulation_exact_predicates.h"
#include <optional>
#include <span>
#include <vector>

namespace clipper2next::internal {
struct monotone_vertex {
    std::size_t index;
    unsigned chain;
};
// Owned by the graph context; layout references are limited to its current build.
struct monotone_workspace final {
    std::vector<monotone_vertex> order;
    std::vector<monotone_vertex> stack;
    auto clear() noexcept -> void;
    auto release() noexcept -> void;
};
struct monotone_layout {
    int direction;
    std::span<const monotone_vertex> order;
};
[[nodiscard]] auto prepare_monotone_layout(const Path64& path,
                                            const triangulation_predicate_domain& domain,
                                            monotone_workspace& workspace) -> std::optional<monotone_layout>;
}  // namespace clipper2next::internal
