#pragma once

#include "clip/engine/private/engine_intersection_processor.h"
#include "clip/engine/private/engine_scanbeam_orchestrator.h"
#include "clip/engine/private/engine_state.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace clipper2next::internal {

class engine_scanbeam_services final {
public:
    engine_scanbeam_services(clipper_base_state& state,
                             engine_scanbeam_orchestration_options options,
                             bool& succeeded,
                             engine_intersection_services& intersection_services) noexcept;

    auto reset() -> void;
    [[nodiscard]] auto pop_scanline(int64_t& y) -> bool;
    auto insert_local_minima_into_ael(int64_t bot_y) -> void;
    [[nodiscard]] auto pop_horz(active_edge_node*& edge) -> bool;
    auto do_horizontal(active_edge_node& edge) -> void;
    auto convert_horz_segments_to_joins() -> void;
    auto do_intersections(int64_t top_y) -> void;
    auto do_top_of_scanbeam(int64_t top_y) -> void;
    auto process_horz_joins() -> void;
    [[nodiscard]] auto succeeded() const -> bool;

private:
    [[nodiscard]] auto build_sorted_intersection_nodes(int64_t top_y,
                                                       bool use_contiguous_unit_runs) -> bool;

    clipper_base_state* state_;
    engine_scanbeam_orchestration_options options_;
    bool* succeeded_;
    engine_intersection_services* intersection_services_;
    std::optional<int64_t> current_x_adjusted_top_y_;
    std::vector<active_edge_node*>& top_edges_at_current_y_;
    std::vector<active_edge_node*>& contiguous_unit_edges_;
    std::vector<active_edge_node*>& contiguous_unit_scratch_;
};

}  // namespace clipper2next::internal
