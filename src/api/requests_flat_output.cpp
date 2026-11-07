#include "api/private/borrowed_topology_pipeline.h"
#include "clip/engine/private/engine_output_cleanup.h"
#include "clip/engine/private/engine_path_builder.h"
#include "clipper2next/core/path_set_builder.h"
#include "support/private/checked_size.h"

#include <optional>

namespace clipper2next::internal {
namespace {
class flat_path_output_transaction final {
public:
    flat_path_output_transaction(path_set64& paths, scoped_workspace_charge& charge) noexcept
        : paths_(paths), charge_(charge), builder_(paths) {}
    ~flat_path_output_transaction() {
        if (!committed_) { builder_.cancel(); charge_.release(); }
    }

    auto begin(std::size_t path_count, std::size_t point_count) -> clipper_error_code {
        const auto bytes = checked_size_add(checked_size_multiply(point_count, sizeof(Point64)),
            checked_size_multiply(path_count, sizeof(geotypes::PathDescriptor)));
        growth_.emplace(&charge_);
        if (const auto error = growth_->try_reserve_slots(bytes, 1U); error != clipper_error_code::ok) {
            return error;
        }
        builder_.begin(path_count, point_count);
        growth_->commit_slots(paths_.point_count(), sizeof(Point64));
        return clipper_error_code::ok;
    }
    auto acquire(std::size_t count) -> std::span<Point64> {
        auto result = builder_.acquire(count, geotypes::PathClosure::ClosedImplicit);
        growth_->commit_slots(paths_.point_count() * sizeof(Point64) +
            paths_.size() * sizeof(geotypes::PathDescriptor), 1U);
        return result;
    }
    auto commit() -> void {
        builder_.finish();
        growth_.reset();
        committed_ = true;
    }

private:
    path_set64& paths_;
    scoped_workspace_charge& charge_;
    path_set_builder64 builder_;
    std::optional<scoped_workspace_growth> growth_{};
    bool committed_{};
};


}  // namespace

auto write_borrowed_closed_paths(
    engine_execution_context* context, const execution_options& options,
    const borrowed_clip_limits64& limits, borrowed_topology_workspace& workspace, path_set64& paths,
    scoped_workspace_charge& output_charge, execution_resource_ledger& resources,
    topology_write_stats64& stats) -> clipper_error_code {
    auto layout = std::span<const closed_ring_descriptor64>{};
    auto point_count = std::size_t{};
    if (context) {
        auto& owner = context->output_owner();
        auto& records = owner.records();
        engine_output_cleanup_options cleanup;
        cleanup.preserve_collinear = options.preserve_collinear;
        cleanup.reverse_solution = options.reverse_solution;
        cleanup.using_polytree = context->state().using_polytree_;
        prepare_output_record_bounds(records, cleanup);
        auto& closed = workspace.closed_layout;
        closed.clear();
        reserve_and_count_growth(closed, records.size(), stats.staging_reallocation_count);
        for (const auto& reference : records) {
            auto* record = reference.get();
            if (!record || record->is_open || !record->pts || record->bounds.is_empty()) { continue; }
            const auto count = path64_point_count(record->pts, options.reverse_solution, false);
            if (!count) { return clipper_error_code::internal_error; }
            point_count = checked_size_add(point_count, count);
            if (closed.size() >= limits.maximum_output_ring_count || point_count > limits.maximum_output_point_count) {
                return clipper_error_code::resource_limit;
            }
            closed.push_back({record, topology_no_polygon_index, topology_ring_role::shell, count});
        }
        layout = {closed.data(), closed.size()};
    }
    flat_path_output_transaction transaction{paths, output_charge};
    if (const auto error = transaction.begin(layout.size(), point_count); error != clipper_error_code::ok) {
        return error;
    }
    for (const auto& item : layout) {
        auto destination = transaction.acquire(item.point_count);
        ++stats.output_ring_acquire_count;
        if (!build_path64_into(item.record->pts, options.reverse_solution, false, destination)) {
            return clipper_error_code::internal_error;
        }
        stats.output_final_point_writes = checked_size_add(stats.output_final_point_writes, destination.size());
    }
    transaction.commit();
    stats.output_ring_count = layout.size();
    stats.output_point_count = point_count;
    stats.peak_workspace_bytes = resources.staging_high_water;
    return clipper_error_code::ok;
}

}  // namespace clipper2next::internal
