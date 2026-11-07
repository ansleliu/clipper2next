#pragma once
#include "clipper2next/core.h"
#include "triangulation/private/triangulation_exact_predicates.h"
#include <optional>

namespace clipper2next::internal {
struct triangulation_context;
class triangulation_operation_lease;
// Stable admitted input and a cleared operation-owned context are required.
// Uncertified domains leave the graph untouched; construction errors propagate.
[[nodiscard]] auto try_build_simple_triangulation_graph(triangulation_context& context,
                                                        const Paths64& paths) -> std::optional<triangulation_predicate_domain>;

[[nodiscard]] auto try_build_simple_triangulation_result(triangulation_context&, const Paths64&, Paths64&)
    -> bool;
[[nodiscard]] auto try_build_simple_triangulation_result(triangulation_context&, const Paths64&, PathsD&, double)
    -> bool;

// A four-vertex input has one diagonal. A certified cocircular ring retains it.
[[nodiscard]] auto try_project_cocircular_quad(triangulation_operation_lease&, const Path64&, Paths64&)
    -> bool;
[[nodiscard]] auto try_project_cocircular_quad(triangulation_operation_lease&, const Path64&, PathsD&, double)
    -> bool;
}  // namespace clipper2next::internal
