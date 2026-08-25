#pragma once

#include "clip/engine/private/engine_scanbeam_orchestrator.h"
#include "clip/engine/private/engine_types.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace clipper2next::internal {

struct global_scanbeam_schedule_result final {
    bool has_inversion{};
    std::size_t active_edge_count{};
    std::size_t inversion_count{};
};

// Prepares the full scanbeam SEL from AEL order. The default unit-run mode is
// the legacy event schedule. Monotone-run mode keeps the SEL global but
// coalesces jump links; keep that mode opt-in and oracle-gated.
[[nodiscard]] auto prepare_global_scanbeam_schedule(
    active_edge_node& head,
    int64_t top_y,
    active_edge_node_ref& sorted_edges,
    std::vector<active_edge_node*>& top_edges)
    -> global_scanbeam_schedule_result;

[[nodiscard]] auto prepare_global_scanbeam_schedule(
    active_edge_node& head,
    int64_t top_y,
    active_edge_node_ref& sorted_edges,
    std::vector<active_edge_node*>& top_edges,
    scanbeam_schedule_mode schedule_mode)
    -> global_scanbeam_schedule_result;

[[nodiscard]] auto prepare_contiguous_unit_scanbeam_schedule(
    active_edge_node& head,
    int64_t top_y,
    std::vector<active_edge_node*>& active_edges,
    std::vector<active_edge_node*>& top_edges)
    -> global_scanbeam_schedule_result;

}  // namespace clipper2next::internal
