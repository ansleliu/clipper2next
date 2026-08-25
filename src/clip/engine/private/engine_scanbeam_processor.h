#pragma once

#include "clip/engine/private/engine_execution_context.h"

namespace clipper2next {
enum class ClipType;
enum class FillRule;
}  // namespace clipper2next

namespace clipper2next::internal {

class engine_scanbeam_services;

class engine_scanbeam_processor final {
public:
    explicit engine_scanbeam_processor(engine_execution_context& context) noexcept
        : context_(&context) {}

    [[nodiscard]] auto context() noexcept -> engine_execution_context& { return *context_; }

    auto execute(engine_scanbeam_services& services,
                 ClipType clip_type,
                 FillRule fill_rule,
                 bool use_polytrees) -> bool;

    auto execute_scanbeam(engine_scanbeam_services& services,
                          ClipType clip_type,
                          FillRule fill_rule,
                          bool use_polytrees) -> bool;

private:
    engine_execution_context* context_;
};

}  // namespace clipper2next::internal
