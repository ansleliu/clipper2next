#pragma once

#include "clip/engine/private/engine_types.h"

namespace clipper2next::internal {

enum class output_path_storage { owning_path, linked_points };

struct engine_output_cleanup_options {
    bool preserve_collinear = true;
    bool reverse_solution = false;
    bool using_polytree = false;
    output_path_storage path_storage = output_path_storage::owning_path;
};

struct cleanup_local_scan_result final {
    bool removed_any = false;
    bool invalidated_path = false;
    output_point_node* stable_start = nullptr;
};

auto compact_local_collinear_nodes(output_record_node& output_record,
                                   const engine_output_cleanup_options& options)
    -> cleanup_local_scan_result;

auto clean_collinear(OutRecList& output_records,
                     output_record_node* output_record,
                     const engine_output_cleanup_options& options) -> void;

auto do_split_operation(OutRecList& output_records,
                        output_record_node* output_record,
                        output_point_node* split_point,
                        const engine_output_cleanup_options& options) -> void;

auto fix_self_intersections(OutRecList& output_records,
                            output_record_node* output_record,
                            const engine_output_cleanup_options& options) -> void;

auto check_bounds(OutRecList& output_records,
                  output_record_node* output_record,
                  const engine_output_cleanup_options& options) -> bool;

auto check_split_owner(OutRecList& output_records,
                       output_record_node* output_record,
                       OutRecList* splits,
                       const engine_output_cleanup_options& options) -> bool;

auto resolve_output_record_owner(OutRecList& output_records,
                                 output_record_node* output_record,
                                 const engine_output_cleanup_options& options) -> void;

auto recursive_check_owners(OutRecList& output_records,
                            output_record_node* output_record,
                            PolyTree64& tree,
                            PolyTree64::node_id parent,
                            const engine_output_cleanup_options& options) -> void;

auto recursive_check_owners(OutRecList& output_records,
                            output_record_node* output_record,
                            PolyTreeD& tree,
                            PolyTreeD::node_id parent,
                            const engine_output_cleanup_options& options) -> void;

}  // namespace clipper2next::internal
