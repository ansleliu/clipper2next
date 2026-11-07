#include "api/private/request_execution_helpers.h"
#include "clip/engine/private/engine_scanbeam_processor.h"

#include "clip/engine/private/engine_execution_context.h"
#include "clip/engine/private/engine_output_cleanup.h"
#include "clip/engine/private/engine_path_builder.h"
#include "clip/engine/private/engine_scanbeam_services.h"
#include "clip/engine/private/engine_winding.h"

#include "support/private/paths_rebuild.h"
#include "clip/engine/private/polygon_tree_rebuild_access.h"

#include <vector>

namespace clipper2next::internal {
auto run_owning_clip(const clip_request64& request,
                     engine_execution_context& context,
                     bool has_open_paths,
                     bool using_polytree,
                     scanbeam_schedule_mode schedule_mode) -> void {
    engine_scanbeam_processor processor{context};
    engine_scanbeam_orchestration_options options;
    options.preserve_collinear = request.options.preserve_collinear;
    options.has_open_paths = has_open_paths;
    options.schedule_mode = schedule_mode;
    bool succeeded = true;
    engine_intersection_services intersections;
    intersections.intersection_policy = request.options.intersection_policy;
    engine_scanbeam_services services{context.state(), options, succeeded, intersections};
    if (!processor.execute(services, request.clip_type, request.fill_rule, using_polytree)) {
        raise_clipper_error(clipper_error_code::internal_error);
    }
}

namespace {
struct path_layout {
    output_record_node* record{};
    std::size_t point_count{};
};
struct node_layout {
    output_record_node* record{};
    std::size_t parent{};
    unsigned depth{};
    std::size_t point_count{};
    std::size_t child_count{};
};
auto prepare_paths(paths_rebuild<int64_t>& transaction, const std::vector<path_layout>& layout)
    -> void {
    transaction.prepare(layout.size(), [&](auto i) { return layout[i].point_count; });
}
auto fill_paths(Paths64& paths, const std::vector<path_layout>& layout, bool reverse) -> void {
    for (std::size_t i = 0; i < layout.size(); ++i) {
        if (!build_path64(layout[i].record->pts, reverse, layout[i].record->is_open, paths[i])) {
            raise_clipper_error(clipper_error_code::internal_error);
        }
    }
}
}  // namespace

auto build_request_clip_paths(engine_execution_context& context,
                              const execution_options& options,
                              Paths64& closed_paths,
                              Paths64& open_paths,
                              bool collect_closed_paths,
                              bool collect_open_paths) -> void {
    auto& records = context.output_owner().records();
    engine_output_cleanup_options cleanup;
    cleanup.preserve_collinear = options.preserve_collinear;
    cleanup.reverse_solution = options.reverse_solution;
    std::vector<path_layout> closed, open;
    for (std::size_t i = 0; i < records.size(); ++i) {
        auto* record = records[i].get();
        if (!record || !record->pts) { continue; }
        if (record->is_open ? !collect_open_paths : !collect_closed_paths) { continue; }
        if (!record->is_open) { clean_collinear(records, record, cleanup); }
        const auto count =
            path64_point_count(record->pts, options.reverse_solution, record->is_open);
        if (count) { (record->is_open ? open : closed).push_back({record, count}); }
    }
    paths_rebuild<int64_t> closed_transaction{closed_paths}, open_transaction{open_paths};
    prepare_paths(closed_transaction, closed);
    prepare_paths(open_transaction, open);
    closed_transaction.begin();
    open_transaction.begin();
    fill_paths(closed_paths, closed, options.reverse_solution);
    fill_paths(open_paths, open, options.reverse_solution);
    closed_transaction.finish();
    open_transaction.finish();
}

auto build_request_clip_tree(engine_execution_context& context,
                             const execution_options& options,
                             PolyTree64& tree,
                             Paths64& open) -> void {
    auto& records = context.output_owner().records();
    engine_output_cleanup_options cleanup;
    cleanup.preserve_collinear = options.preserve_collinear;
    cleanup.reverse_solution = options.reverse_solution;
    cleanup.using_polytree = true;
    std::vector<path_layout> open_layout;
    for (std::size_t i = 0; i < records.size(); ++i) {
        auto* record = records[i].get();
        if (!record || !record->pts) { continue; }
        if (record->is_open) {
            const auto count = path64_point_count(record->pts, options.reverse_solution, true);
            if (count) { open_layout.push_back({record, count}); }
        } else {
            static_cast<void>(check_bounds(records, record, cleanup));
        }
    }
    // Owner resolution and layout are engine work; the caller's tree is untouched.
    std::vector<node_layout> layout(1);
    auto append = [&](auto&& self, output_record_node* record) -> void {
        if (record->polygon_node || record->bounds.is_empty()) { return; }
        resolve_output_record_owner(records, record, cleanup);
        auto parent = std::size_t{};
        if (record->owner) {
            self(self, record->owner.get());
            parent = *record->owner->polygon_node;
        }
        const auto index = layout.size();
        const auto count = path64_point_count(record->pts, options.reverse_solution, false);
        if (!count) { raise_clipper_error(clipper_error_code::internal_error); }
        const auto depth = layout[parent].depth + 1U;
        layout.push_back({record, parent, depth, count, 0U});
        ++layout[parent].child_count;
        record->polygon_node = index;
    };
    for (std::size_t i = 0; i < records.size(); ++i) {
        auto* record = records[i].get();
        if (record && !record->is_open && record->pts) { append(append, record); }
    }
    using access = polygon_tree_rebuild_access;
    const auto original = access::size(tree);
    paths_rebuild<int64_t> open_transaction{open};
    bool begun = false;
    try {
        access::prepare(tree, layout);
        prepare_paths(open_transaction, open_layout);
        begun = true;
        access::begin(tree);
        open_transaction.begin();
        for (std::size_t i = 1; i < layout.size(); ++i) {
            const auto& item = layout[i];
            if (!build_path64(
                    item.record->pts, options.reverse_solution, false, access::polygon(tree, i))) {
                raise_clipper_error(clipper_error_code::internal_error);
            }
            access::link(tree, i, item.parent, item.depth);
        }
        fill_paths(open, open_layout, options.reverse_solution);
        access::resize(tree, layout.size());
        open_transaction.finish();
    } catch (...) {
        if (begun) {
            access::cancel(tree);
        } else {
            access::resize(tree, original);
        }
        throw;
    }
}

}  // namespace clipper2next::internal
