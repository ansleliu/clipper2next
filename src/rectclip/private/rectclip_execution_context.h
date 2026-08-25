#pragma once

#include "rectclip/private/rectclip_context.h"
#include "rectclip/private/rectclip_intersections.h"
#include "rectclip/private/rectclip_traversal.h"

namespace clipper2next::internal {

class rectclip_execution_context final {
public:
    explicit rectclip_execution_context(rectclip_context& context) noexcept
        : context_(&context) {}

    [[nodiscard]] auto storage() noexcept -> rectclip_context& { return *context_; }

    auto add(Point64 point, bool start_new = false) -> rectclip_node&;

    auto add_corner(rect_location previous, rect_location current) -> void;

    auto add_corner(rect_location& location, bool clockwise) -> void;

    auto get_next_location(const Path64& path,
                           rect_location& location,
                           size_t& index,
                           size_t high_index) -> void;

private:
    rectclip_context* context_;
};

}  // namespace clipper2next::internal
