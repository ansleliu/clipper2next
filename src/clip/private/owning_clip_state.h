#pragma once

#include "clip/engine/private/engine_state.h"
#include <optional>

namespace clipper2next::internal {
struct owning_clip_state_slot;
class owning_clip_state_lease final {
public:
    owning_clip_state_lease();
    owning_clip_state_lease(const owning_clip_state_lease&) = delete;
    auto operator=(const owning_clip_state_lease&) -> owning_clip_state_lease& = delete;
    ~owning_clip_state_lease();
    [[nodiscard]] auto state() noexcept -> clipper_base_state& { return *state_; }
private:
    std::optional<clipper_base_state> local_{};
    owning_clip_state_slot* slot_{};
    clipper_base_state* state_{};
};
}  // namespace clipper2next::internal
