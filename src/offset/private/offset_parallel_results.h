#pragma once

#include "offset/private/offset_group_processor.h"
#include "support/private/engine_resource_plan.h"
#include <span>
#include <vector>

namespace clipper2next::internal {

struct offset_path_chunk final {
    std::size_t begin{};
    std::size_t end{};
};
struct offset_chunk_result final {
    execution_resource_ledger resources{};
    scoped_workspace_charge output_charge{};
    path_set64 paths;
    std::size_t retained_bytes{};
};

// Owns one synchronous parallel phase: independent worker admission, actual
// simultaneous observation, and the completed chunks retained during merge.
class offset_parallel_results final {
public:
    offset_parallel_results(execution_resource_ledger* root, const offset_group& group,
                            const offset_group_execution_options& options, double delta,
                            std::span<const offset_path_chunk> chunks, std::size_t active_concurrency)
        : root_(root), planning_charge_(root, workspace_category::staging),
          planning_growth_(planning_charge_, checked_size_multiply(chunks.size(),
              checked_size_add(sizeof(offset_chunk_result), sizeof(parallel_engine_workspace_slot))), 1U),
          reservation_(root), observer_slots_(chunks.size()), observer_(std::span{observer_slots_}),
          results_(chunks.size()), chunks_owner_(root) {
        planning_growth_.commit();
        auto retained_bound = std::size_t{};
        auto maximum_scratch = std::size_t{};
        for (std::size_t i = 0; root && i < chunks.size(); ++i) {
            auto points = std::size_t{};
            auto maximum = std::size_t{};
            for (auto j = chunks[i].begin; j < chunks[i].end; ++j) {
                points = checked_size_add(points, group.path(j).size());
                maximum = std::max(maximum, group.path(j).size());
            }
            const auto plan = plan_offset_generation_resources(chunks[i].end - chunks[i].begin,
                points, maximum, delta, group.join_type, group.end_type,
                options.arc_tolerance, options.arc_segments_per_quadrant, 1U);
            auto& child = results_[i];
            child.resources.maximum_workspace_bytes = checked_size_add(plan.retained_output_bytes, plan.scratch_bytes);
            child.resources.maximum_staging_bytes = 0U;
            child.resources.parallel_observer = &observer_slots_[i];
            child.output_charge = scoped_workspace_charge{&child.resources};
            retained_bound = checked_size_add(retained_bound, plan.retained_output_bytes);
            maximum_scratch = std::max(maximum_scratch, plan.scratch_bytes);
        }
        // Every completed chunk retains its raw output. Only executing tasks
        // retain scratch, one chunk per worker at a time. The synchronous
        // executor contract bounds these by active_concurrency, not chunks.size().
        reservation_.set_bytes(checked_size_add(retained_bound,
            checked_size_multiply(std::min(active_concurrency, chunks.size()), maximum_scratch)));
    }
    offset_parallel_results(const offset_parallel_results&) = delete;
    auto operator=(const offset_parallel_results&) -> offset_parallel_results& = delete;
    ~offset_parallel_results() {
        // Also handles worker rejection and a partially consumed merge. No
        // token may retain the frame observer after this owner is destroyed.
        for (auto& result : results_) {
            result.paths.clear();
            result.output_charge.release();
            result.resources.parallel_observer = nullptr;
        }
        chunks_owner_.release();
    }
    auto operator[](std::size_t index) noexcept -> offset_chunk_result& { return results_[index]; }
    [[nodiscard]] auto size() const noexcept -> std::size_t { return results_.size(); }
    auto observe_quiescence() -> void {
        // The executor has joined all workers. Parent live bytes were fixed
        // during execute; quota never contributed to actual live high-water.
        if (root_) {
            root_->engine_high_water = std::max(root_->engine_high_water,
                checked_size_add(root_->engine_bytes, observer_.peak()));
        }
    }
    auto prepare_merge() -> void {
        auto retained = std::size_t{};
        for (auto& result : results_) {
            result.retained_bytes = result.output_charge.bytes();
            assert(result.resources.engine_bytes == result.retained_bytes);
            retained = checked_size_add(retained, result.retained_bytes);
        }
        assert(retained == observer_.live());
        // Replace quota with actual completed owners once, at quiescence.
        // Actual <= admitted quota, so this parent admission cannot fail.
        reservation_.release();
        chunks_owner_.set_live_slots(retained, 1U);
        for (auto& result : results_) {
            result.resources.parallel_observer = nullptr;
            result.output_charge.release();
        }
        observer_.reset_after_transfer();
    }
    auto consume(std::size_t index) -> void {
        auto& result = results_[index];
        result.paths.clear();
        chunks_owner_.set_live_slots(chunks_owner_.bytes() - result.retained_bytes, 1U);
        result.retained_bytes = 0U;
    }
private:
    execution_resource_ledger* root_{};
    scoped_workspace_charge planning_charge_;
    scoped_workspace_growth planning_growth_;
    scoped_workspace_reservation reservation_;
    // Child charges refer to slots and their observer, including when the
    // constructor body throws. Their storage must be destroyed first.
    // The admitted slot buffer provides the type's alignment without
    // imposing over-alignment on the large automatic execution frame.
    std::vector<parallel_engine_workspace_slot> observer_slots_;
    parallel_engine_workspace_observer observer_;
    std::vector<offset_chunk_result> results_;
    scoped_workspace_charge chunks_owner_;
};

} // namespace clipper2next::internal
