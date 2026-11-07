#pragma once

#include "clipper2next/clip/topology.h"
#include "clipper2next/core/path_set.h"
#include "clip/engine/private/engine_execution_context.h"
#include "clip/engine/private/engine_state.h"

#include <cstddef>
#include <limits>
#include <span>
#include <optional>
#include <vector>

namespace clipper2next::internal {

struct measured_paths64 final {
    staging_buffer<path_source_contract::borrowed_path_measurement64> paths{};
    std::size_t source_point_count{};
    std::size_t normalized_point_count{};
};

struct closed_ring_descriptor64 final {
    output_record_node* record{};
    // Flat output has no polygon association; topology assigns relation fields.
    std::size_t polygon_index{topology_no_polygon_index};
    topology_ring_role role{topology_ring_role::shell};
    std::size_t point_count{};
};

struct topology_record_metadata64 final {
    std::size_t polygon_index{topology_no_polygon_index};
    std::size_t point_count{};
    bool is_hole{};
};

struct borrowed_topology_workspace final {
    measured_paths64 subjects{};
    measured_paths64 clips{};
    staging_buffer<topology_polygon_layout64> polygon_layouts{};
    staging_buffer<closed_ring_descriptor64> closed_layout{};
    staging_buffer<topology_record_metadata64> record_metadata{};
    staging_buffer<std::size_t> next_ring_by_polygon{};

    auto clear() noexcept -> void;
    auto release() noexcept -> void;
};

struct borrowed_topology_engine_state_slot;

class borrowed_topology_engine_state_lease final {
public:
    explicit borrowed_topology_engine_state_lease(execution_resource_ledger* resources = nullptr);
    borrowed_topology_engine_state_lease(const borrowed_topology_engine_state_lease&) = delete;
    auto operator=(const borrowed_topology_engine_state_lease&)
        -> borrowed_topology_engine_state_lease& = delete;
    ~borrowed_topology_engine_state_lease();

    [[nodiscard]] auto state() noexcept -> clipper_base_state& { return *state_; }
    [[nodiscard]] auto workspace() noexcept -> borrowed_topology_workspace& { return *workspace_; }
    [[nodiscard]] auto prepare_engine() noexcept -> clipper_base_state&;

private:
    auto bind_workspace_resources() -> void;
    auto bind_engine_resources() -> void;
    auto detach_resources() noexcept -> void;
    execution_resource_ledger* resources_{};
    std::optional<clipper_base_state> local_state_{};
    std::optional<borrowed_topology_workspace> local_workspace_{};
    borrowed_topology_engine_state_slot* slot_{};
    clipper_base_state* state_{};
    borrowed_topology_workspace* workspace_{};
    bool engine_prepared_{};
};

[[nodiscard]] inline auto exceeds(std::size_t value, std::size_t maximum) noexcept -> bool {
    return value > maximum;
}

[[nodiscard]] inline auto checked_accumulate(std::size_t& target, std::size_t value) noexcept
    -> clipper_error_code {
    if (value > (std::numeric_limits<std::size_t>::max)() - target) {
        return clipper_error_code::resource_limit;
    }
    target += value;
    return clipper_error_code::ok;
}

template <typename T, workspace_category Category>
auto resize_and_count_growth(
    engine_buffer<T, Category>& values, std::size_t size, std::size_t& reallocation_count) -> void {
    const auto previous_capacity = values.capacity();
    values.resize(size);
    if (values.capacity() != previous_capacity) { ++reallocation_count; }
}

template <typename T, workspace_category Category>
auto reserve_and_count_growth(
    engine_buffer<T, Category>& values, std::size_t capacity, std::size_t& reallocation_count) -> void {
    const auto previous_capacity = values.capacity();
    values.reserve(capacity);
    if (values.capacity() != previous_capacity) { ++reallocation_count; }
}

[[nodiscard]] auto measure_paths(const borrowed_paths64& source,
                                 measured_paths64& result,
                                 const borrowed_clip_limits64& limits,
                                 std::size_t& total_path_count,
                                 std::size_t& total_point_count,
                                 std::size_t& reallocation_count)
    -> clipper_error_code;

[[nodiscard]] auto load_paths(clipper_base_state& state,
                              const borrowed_paths64& source,
                              const measured_paths64& measured,
                              PathType path_type,
                              topology_write_stats64& stats) -> clipper_error_code;

[[nodiscard]] auto build_topology_descriptors(
    engine_execution_context& context,
    const execution_options& options,
    const borrowed_clip_limits64& limits,
    borrowed_topology_workspace& workspace,
    std::size_t& maximum_ring_point_count,
    std::size_t& reallocation_count) -> clipper_error_code;

[[nodiscard]] auto write_topology(
    topology_writer64& writer,
    const execution_options& options,
    std::span<const topology_polygon_layout64> polygon_layouts,
    std::span<const closed_ring_descriptor64> rings,
    std::size_t maximum_ring_point_count,
    std::size_t previous_peak_workspace_bytes,
    topology_write_stats64& stats) -> clipper_error_code;

[[nodiscard]] auto execute_borrowed_topology(
    const borrowed_clip_request64& request, topology_writer64& writer,
    execution_resource_ledger* resources = nullptr)
    -> clipper_result<topology_write_stats64>;

[[nodiscard]] auto write_borrowed_closed_paths(
    engine_execution_context* context, const execution_options& options,
    const borrowed_clip_limits64& limits, borrowed_topology_workspace& workspace, path_set64& paths,
    scoped_workspace_charge& output_charge, execution_resource_ledger& resources,
    topology_write_stats64& stats) -> clipper_error_code;

[[nodiscard]] auto execute_borrowed_closed_paths(
    const borrowed_clip_request64& request, path_set64& paths,
    scoped_workspace_charge& output_charge) -> clipper_error_code;

auto release_borrowed_topology_thread_state() noexcept -> void;

}  // namespace clipper2next::internal
