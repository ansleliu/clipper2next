#pragma once

#include "offset/private/offset_execution_context.h"

namespace clipper2next::internal {

class offset_path_processor final {
public:
    explicit offset_path_processor(offset_execution_context& context) noexcept
        : context_(&context) {}

    [[nodiscard]] auto context() noexcept -> offset_execution_context& { return *context_; }

    auto build_normals(const Path64& path) -> void;

private:
    offset_execution_context* context_;
};

}  // namespace clipper2next::internal
