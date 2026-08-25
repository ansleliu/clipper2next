#pragma once

#include "clipper2next/clip/request.h"
#include "clipper2next/polygon/poly_tree.h"

namespace clipper2next::internal {

struct clip_union_options final {
    FillRule fill_rule{FillRule::NonZero};
    execution_options options{};
    bool decompose_disjoint_components{true};
    bool use_offset_cleanup_monotone_scanbeam_runs{false};
};

[[nodiscard]] auto try_return_singleton_component_direct(Path64& path,
                                                         const clip_union_options& options)
    -> bool;
[[nodiscard]] auto try_append_singleton_component_direct(Path64& path,
                                                         const clip_union_options& options,
                                                         paths64_result& result) -> bool;

[[nodiscard]] auto union_paths(const Paths64& paths, const clip_union_options& options = {})
    -> paths64_result;
[[nodiscard]] auto union_paths(Paths64&& paths, const clip_union_options& options = {})
    -> paths64_result;

[[nodiscard]] auto union_closed_paths(const Paths64& paths, const clip_union_options& options = {})
    -> Paths64;
[[nodiscard]] auto union_closed_paths(Paths64&& paths, const clip_union_options& options = {})
    -> Paths64;

auto union_closed_paths_into_tree(const Paths64& paths,
                                  PolyTree64& tree,
                                  const clip_union_options& options = {}) -> void;

}  // namespace clipper2next::internal
