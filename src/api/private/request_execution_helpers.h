#pragma once

#include "clipper2next/clip/request.h"
#include "clip/engine/private/engine_execution_context.h"
#include "clip/engine/private/engine_scanbeam_orchestrator.h"

namespace clipper2next::internal {
auto run_owning_clip(const clip_request64& request,
                     engine_execution_context& context,
                     bool has_open_paths,
                     bool using_polytree,
                     scanbeam_schedule_mode schedule_mode = scanbeam_schedule_mode::unit_runs) -> void;
auto build_request_clip_paths(engine_execution_context& context,
                              const execution_options& options,
                              Paths64& closed,
                              Paths64& open,
                              bool collect_closed_paths,
                              bool collect_open_paths) -> void;
auto build_request_clip_tree(engine_execution_context& context,
                             const execution_options& options,
                             PolyTree64& tree,
                             Paths64& open) -> void;
}  // namespace clipper2next::internal
