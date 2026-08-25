#pragma once

#include "offset/private/offset_state.h"
#include "clipper2next/offset/types.h"
#include "clipper2next/api/options.h"
#include "clipper2next/polygon/poly_tree.h"

namespace clipper2next::internal {

class offset_execution_context final {
public:
    offset_execution_context(offset_state& state,
                             Paths64& paths_solution,
                             PolyTree64* poly_tree_solution,
                             const execution_options& options,
                             delta_callback_ref callback) noexcept;

    [[nodiscard]] auto state() noexcept -> offset_state&;
    [[nodiscard]] auto paths_solution() noexcept -> Paths64&;
    [[nodiscard]] auto poly_tree_solution() const noexcept -> PolyTree64*;
    [[nodiscard]] auto options() const noexcept -> const execution_options&;
    [[nodiscard]] auto delta_callback() const noexcept -> delta_callback_ref;

private:
    offset_state* state_;
    Paths64* paths_solution_;
    PolyTree64* poly_tree_solution_;
    const execution_options* options_;
    delta_callback_ref callback_;
};

}  // namespace clipper2next::internal
