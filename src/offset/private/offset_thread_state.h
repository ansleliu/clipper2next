#pragma once

#include "offset/private/offset_state.h"
#include <optional>
namespace clipper2next::internal {

struct offset_state_slot;
class offset_state_lease final {
public:
    offset_state_lease();
    offset_state_lease(const offset_state_lease&) = delete;
    auto operator=(const offset_state_lease&) -> offset_state_lease& = delete;
    ~offset_state_lease();
    [[nodiscard]] auto state() noexcept -> offset_state& { return *state_; }
private:
    std::optional<offset_state> local_;
    offset_state_slot* slot_{};
    offset_state* state_{};
};
auto release_offset_thread_state() noexcept -> void;
}  // namespace clipper2next::internal
