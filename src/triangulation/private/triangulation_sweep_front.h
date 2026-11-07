#pragma once
#include "triangulation/private/triangulation_context.h"
#include "triangulation/private/triangulation_boundary_events.h"

namespace clipper2next::internal {

// The admitted boundary and current sweep minimum define the unfilled frontier.
// Future vertices are outside this phase; filled sides have left the frontier.
class triangulation_sweep_front final {
public:
    triangulation_sweep_front(triangulation_context& context, Point64 minimum,
                              triangulation_predicate_domain domain) noexcept
        : context_(context), minimum_(minimum), domain_(domain) {}
    [[nodiscard]] auto context() const noexcept -> triangulation_context& { return context_; }
    [[nodiscard]] auto minimum() const noexcept -> Point64 { return minimum_; }
    [[nodiscard]] auto numeric_domain() const noexcept -> const triangulation_predicate_domain& { return domain_; }
    [[nodiscard]] auto includes(Point64 point) const noexcept -> bool {
        return !sweep_before(minimum_, point);
    }
private:
    triangulation_context& context_;
    const Point64 minimum_;
    const triangulation_predicate_domain domain_;
};

}  // namespace clipper2next::internal
