#pragma once

#include "clip/engine/private/engine_intersection_processor.h"
#include "clip/engine/private/engine_state.h"

#include <cstdint>
#include <span>

namespace clipper2next::internal {

enum class scanbeam_schedule_mode {
    unit_runs,
    monotone_runs,
};

struct engine_scanbeam_orchestration_options {
    bool preserve_collinear = true;
    bool has_open_paths = false;
    scanbeam_schedule_mode schedule_mode{scanbeam_schedule_mode::unit_runs};
};

auto do_horizontal(clipper_base_state& state,
                   active_edge_node& horizontal,
                   const engine_scanbeam_orchestration_options& options,
                   bool& succeeded) -> void;

auto do_top_of_scanbeam(clipper_base_state& state,
                        int64_t top_y,
                        const engine_scanbeam_orchestration_options& options,
                        bool& succeeded,
                        bool current_x_already_at_top = false) -> void;

auto do_known_top_edges_of_scanbeam(clipper_base_state& state,
                                    int64_t top_y,
                                    std::span<active_edge_node* const> top_edges,
                                    const engine_scanbeam_orchestration_options& options,
                                    bool& succeeded) -> void;

auto do_maxima(clipper_base_state& state,
               active_edge_node& edge,
               const engine_scanbeam_orchestration_options& options,
               bool& succeeded) -> active_edge_node*;

}  // namespace clipper2next::internal
