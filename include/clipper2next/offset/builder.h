#pragma once

#include "clipper2next/api/export.h"
#include "clipper2next/api/options.h"
#include "clipper2next/offset/types.h"
#include "clipper2next/polygon/poly_tree.h"
#include "clipper2next/api/result.h"

#include <memory>

namespace clipper2next {

class offset_builder final {
public:
    CLIPPER2NEXT_API offset_builder();
    CLIPPER2NEXT_API offset_builder(const offset_builder&);
    CLIPPER2NEXT_API auto operator=(const offset_builder&) -> offset_builder&;
    CLIPPER2NEXT_API offset_builder(offset_builder&&) noexcept;
    CLIPPER2NEXT_API auto operator=(offset_builder&&) noexcept -> offset_builder&;
    CLIPPER2NEXT_API ~offset_builder();

    CLIPPER2NEXT_API auto delta(double value) -> offset_builder&;
    CLIPPER2NEXT_API auto join(JoinType value) -> offset_builder&;
    CLIPPER2NEXT_API auto end(EndType value) -> offset_builder&;
    CLIPPER2NEXT_API auto miter_limit(double value) -> offset_builder&;
    CLIPPER2NEXT_API auto arc_tolerance(double value) -> offset_builder&;
    // cppcheck-suppress passedByValue
    CLIPPER2NEXT_API auto options(execution_options value) -> offset_builder&;
    CLIPPER2NEXT_API auto preserve_collinear(bool value) -> offset_builder&;
    CLIPPER2NEXT_API auto reverse_solution(bool value) -> offset_builder&;
    CLIPPER2NEXT_API auto delta_callback(DeltaCallback64 callback) -> offset_builder&;

    CLIPPER2NEXT_API auto add(const Path64& path) -> offset_builder&;
    CLIPPER2NEXT_API auto add(const Paths64& paths) -> offset_builder&;
    CLIPPER2NEXT_API auto clear() -> offset_builder&;

    [[nodiscard]] CLIPPER2NEXT_API auto execute() const -> Paths64;
    CLIPPER2NEXT_API auto execute_into(Paths64& solution) const -> void;
    CLIPPER2NEXT_API auto execute_into(PolyTree64& solution) const -> void;

private:
    struct impl;
    std::unique_ptr<impl> impl_;
};

}  // namespace clipper2next
