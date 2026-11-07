#pragma once

#include "clip/engine/private/engine_types.h"
#include "support/private/storage/stable_pool.h"

namespace clipper2next::internal {

class engine_output_owner final {
public:
    engine_output_owner() = default;
    engine_output_owner(const engine_output_owner&) = delete;
    auto operator=(const engine_output_owner&) -> engine_output_owner& = delete;
    ~engine_output_owner();

    [[nodiscard]] auto resources() const noexcept -> execution_resource_ledger* { return resources_; }
    auto bind(execution_resource_ledger* context) -> void {
        resources_ = context;
        record_pool_.bind(context); point_pool_.bind(context); records_.bind(context);
    }
    auto detach() noexcept -> void {
        for (auto record : records_) { record->splits.detach(); }
        record_pool_.detach(); point_pool_.detach(); records_.detach(); resources_ = nullptr;
    }
    [[nodiscard]] auto create_outrec() -> output_record_node& {
        auto& result = record_pool_.emplace_direct(records_.size(), *this, resources_);
        records_.push_back(output_record_node_ref{result});
        return result;
    }

    [[nodiscard]] auto create_outpt(const Point64& point, output_record_node& output_record)
        -> output_point_node& {
        return point_pool_.emplace_direct(point, output_record);
    }

    auto dispose_all() noexcept -> void;

    // Unlike dispose_all(), also returns the pool storage to the allocator.
    auto release() noexcept -> void;

    [[nodiscard]] auto records() noexcept -> OutRecList& { return records_; }
    [[nodiscard]] auto records() const noexcept -> const OutRecList& { return records_; }

private:
    execution_resource_ledger* resources_{};
    stable_pool<output_record_node> record_pool_{};
    stable_pool<output_point_node> point_pool_{};
    OutRecList records_{};
};

}  // namespace clipper2next::internal
