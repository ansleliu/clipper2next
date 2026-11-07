#pragma once

#include "triangulation/private/triangulation_cache.h"
#include "triangulation/private/triangulation_context.h"

#include <optional>

namespace clipper2next::internal {

struct triangulation_operation_state final {
    std::optional<triangulation_context> graph;
    std::optional<triangulation_cache_state> result_cache;

    auto release() noexcept -> void;
};

struct triangulation_operation_slot;

// A graph or cache reference is borrowed only while this operation remains alive.
class triangulation_operation_lease final {
public:
    triangulation_operation_lease();
    ~triangulation_operation_lease() noexcept;
    triangulation_operation_lease(const triangulation_operation_lease&) = delete;
    auto operator=(const triangulation_operation_lease&) -> triangulation_operation_lease& = delete;
    triangulation_operation_lease(triangulation_operation_lease&&) = delete;
    auto operator=(triangulation_operation_lease&&) -> triangulation_operation_lease& = delete;

    [[nodiscard]] auto context() -> triangulation_context&;
    [[nodiscard]] auto cache() -> triangulation_cache_state&;
    [[nodiscard]] auto existing_cache() noexcept -> triangulation_cache_state* {
        return state_->result_cache ? &*state_->result_cache : nullptr;
    }

private:
    std::optional<triangulation_operation_state> local_;
    triangulation_operation_state* state_{};
    triangulation_operation_slot* slot_{};
    triangulation_context* prepared_graph_{};
};

auto release_triangulation_thread_state() noexcept -> void;

}  // namespace clipper2next::internal
