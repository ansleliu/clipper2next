#pragma once
#include "triangulation/private/triangulation_sweep_front.h"
#include <optional>
#include <utility>

namespace clipper2next::internal {

// Valid through the selector's read-only interval. Creation consumes the proof
// before any graph allocation or topology change; no proof is kept in a cache.
class triangulation_sweep_diagonal final {
public:
    triangulation_sweep_diagonal(const triangulation_sweep_diagonal&) = delete;
    auto operator=(const triangulation_sweep_diagonal&) -> triangulation_sweep_diagonal& = delete;
    triangulation_sweep_diagonal(triangulation_sweep_diagonal&& other) noexcept
        : front_(other.front_), first_(std::exchange(other.first_, nullptr)),
          second_(std::exchange(other.second_, nullptr)) {}
    auto operator=(triangulation_sweep_diagonal&&) -> triangulation_sweep_diagonal& = delete;
    [[nodiscard]] static auto probe(const triangulation_sweep_front& front,
                                     triangulation_vertex* first, triangulation_vertex* second)
        -> std::optional<triangulation_sweep_diagonal>;
    [[nodiscard]] auto front() const noexcept -> const triangulation_sweep_front& { return front_; }
    [[nodiscard]] auto first() const noexcept -> triangulation_vertex* { return first_; }
    [[nodiscard]] auto second() const noexcept -> triangulation_vertex* { return second_; }
private:
    triangulation_sweep_diagonal(const triangulation_sweep_front& front,
                                 triangulation_vertex* first, triangulation_vertex* second) noexcept
        : front_(front), first_(first), second_(second) {}
    friend auto create_sweep_loose_edge(triangulation_sweep_diagonal&& proof) -> triangulation_edge*;
    triangulation_sweep_front front_;
    triangulation_vertex* first_;
    triangulation_vertex* second_;
};

}  // namespace clipper2next::internal
