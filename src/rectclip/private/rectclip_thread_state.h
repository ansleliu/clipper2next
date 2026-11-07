#pragma once

#include "rectclip/private/rectclip_context.h"

#include <optional>
#include <vector>

namespace clipper2next::internal {

struct rectclip_operation_state final {
    std::optional<rectclip_context> context{};
    std::vector<Rect64> bounds{};
    auto reset() noexcept -> void;
    auto release() noexcept -> void;
};

struct rectclip_slot;

// One synchronous owner protects both cached geometry and its bounds input.
class rectclip_operation_lease final {
public:
    rectclip_operation_lease();
    rectclip_operation_lease(const rectclip_operation_lease&) = delete;
    auto operator=(const rectclip_operation_lease&) -> rectclip_operation_lease& = delete;
    ~rectclip_operation_lease();
    [[nodiscard]] auto context(const Rect64&) -> rectclip_context&;
    [[nodiscard]] auto bounds() noexcept -> std::vector<Rect64>& { return state_->bounds; }
private:
    rectclip_slot* slot_{};
    rectclip_operation_state* state_{};
    std::optional<rectclip_operation_state> local_{};
};

auto release_rectclip_thread_state() noexcept -> void;

}  // namespace clipper2next::internal
