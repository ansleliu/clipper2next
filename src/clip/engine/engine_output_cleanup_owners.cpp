#include "clip/engine/private/engine_output_cleanup.h"

#include "clip/engine/private/engine_topology.h"

#include <cstdint>
#include <type_traits>
#include <utility>

namespace clipper2next::internal {
namespace {

template <typename Tree>
[[nodiscard]] auto add_output_record_child(Tree& tree,
                                           typename Tree::node_id parent,
                                           output_record_node* output_record)
    -> typename Tree::node_id {
    if constexpr (std::is_same_v<typename Tree::coordinate_type, std::int64_t>) {
        return tree.add_child(parent, std::move(output_record->path));
    } else {
        return tree.add_child(parent, output_record->path);
    }
}

template <typename Tree>
auto recursive_check_owners_impl(OutRecList& output_records,
                                 output_record_node* output_record,
                                 Tree& tree,
                                 typename Tree::node_id parent,
                                 const engine_output_cleanup_options& options) -> void {
    if (output_record->polygon_node.has_value() || output_record->bounds.is_empty()) { return; }

    resolve_output_record_owner(output_records, output_record, options);

    if (output_record->owner) {
        if (!output_record->owner->polygon_node.has_value()) {
            recursive_check_owners_impl(
                output_records, output_record->owner, tree, parent, options);
        }
        const auto owner_node = typename Tree::node_id{*output_record->owner->polygon_node};
        output_record->polygon_node =
            add_output_record_child(tree, owner_node, output_record).value;
    } else {
        output_record->polygon_node =
            add_output_record_child(tree, parent, output_record).value;
    }
}

}  // namespace

auto resolve_output_record_owner(OutRecList& output_records,
                                 output_record_node* output_record,
                                 const engine_output_cleanup_options& options) -> void {
    if (!output_record || output_record->bounds.is_empty()) { return; }
    while (output_record->owner) {
        if (!output_record->owner->splits.empty() &&
            check_split_owner(
                output_records, output_record, &output_record->owner->splits, options)) {
            break;
        }
        if (output_record->owner->pts &&
            check_bounds(output_records, output_record->owner, options) &&
            output_record->owner->bounds.contains(output_record->bounds) &&
            path2_contains_path1(output_record->pts, output_record->owner->pts)) {
            break;
        }
        output_record->owner = output_record->owner->owner;
    }
}

auto recursive_check_owners(OutRecList& output_records,
                            output_record_node* output_record,
                            PolyTree64& tree,
                            PolyTree64::node_id parent,
                            const engine_output_cleanup_options& options) -> void {
    recursive_check_owners_impl(output_records, output_record, tree, parent, options);
}

auto recursive_check_owners(OutRecList& output_records,
                            output_record_node* output_record,
                            PolyTreeD& tree,
                            PolyTreeD::node_id parent,
                            const engine_output_cleanup_options& options) -> void {
    recursive_check_owners_impl(output_records, output_record, tree, parent, options);
}

}  // namespace clipper2next::internal
