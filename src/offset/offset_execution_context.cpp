#include "offset/private/offset_execution_context.h"

namespace clipper2next::internal {

offset_execution_context::offset_execution_context(offset_state& state,
                                                   Paths64& paths_solution,
                                                   PolyTree64* poly_tree_solution,
                                                   const execution_options& options,
                                                   delta_callback_ref callback) noexcept
    : state_(&state),
      paths_solution_(&paths_solution),
      poly_tree_solution_(poly_tree_solution),
      options_(&options),
      callback_(callback) {}

auto offset_execution_context::state() noexcept -> offset_state& {
    return *state_;
}

auto offset_execution_context::paths_solution() noexcept -> Paths64& {
    return *paths_solution_;
}

auto offset_execution_context::poly_tree_solution() const noexcept -> PolyTree64* {
    return poly_tree_solution_;
}

auto offset_execution_context::options() const noexcept -> const execution_options& {
    return *options_;
}

auto offset_execution_context::delta_callback() const noexcept -> delta_callback_ref {
    return callback_;
}

}  // namespace clipper2next::internal
