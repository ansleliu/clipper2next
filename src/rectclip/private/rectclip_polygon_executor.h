#pragma once

#include "clipper2next/core.h"
#include "rectclip/private/rectclip_context.h"

namespace clipper2next {
namespace internal {
class rectclip_execution_context;

class rectclip_polygon_executor {
public:
    explicit rectclip_polygon_executor(rectclip_context& storage) noexcept;

    [[nodiscard]] auto storage() noexcept -> rectclip_context&;
    [[nodiscard]] auto storage() const noexcept -> const rectclip_context&;
    auto execute_path(rectclip_execution_context& execution, const Path64& path) -> void;

private:
    rectclip_context* storage_;
};
}  // namespace internal
}  // namespace clipper2next
