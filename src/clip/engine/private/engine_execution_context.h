#pragma once

#include "clip/engine/private/engine_state.h"

namespace clipper2next::internal {

class engine_execution_context final {
public:
    explicit engine_execution_context(clipper_base_state& state) noexcept
        : state_(&state) {}

    [[nodiscard]] auto state() noexcept -> clipper_base_state& { return *state_; }
    [[nodiscard]] auto state() const noexcept -> const clipper_base_state& { return *state_; }
    [[nodiscard]] auto output_owner() noexcept -> engine_output_owner& {
        return state_->output_owner_;
    }

private:
    clipper_base_state* state_;
};

}  // namespace clipper2next::internal
