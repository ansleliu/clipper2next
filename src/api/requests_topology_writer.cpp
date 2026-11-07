#include "api/private/borrowed_topology_pipeline.h"
#include "clip/engine/private/engine_path_builder.h"
#include "clip/private/borrowed_topology_access.h"
#include "support/private/checked_size.h"

namespace clipper2next::internal {
namespace {
class writer_transaction final {
public:
    explicit writer_transaction(topology_writer64& writer) noexcept : writer_(&writer) {}
    writer_transaction(const writer_transaction&) = delete;
    auto operator=(const writer_transaction&) -> writer_transaction& = delete;
    ~writer_transaction() {
        if (active_) { topology_writer64_access::cancel(*writer_); }
    }

    auto activate() noexcept -> void { active_ = true; }
    auto commit() noexcept -> void { active_ = false; }

private:
    topology_writer64* writer_;
    bool active_{};
};


}  // namespace

auto write_topology(topology_writer64& writer,
                    const execution_options& options,
                    std::span<const topology_polygon_layout64> polygon_layouts,
                    std::span<const closed_ring_descriptor64> rings,
                    std::size_t maximum_ring_point_count,
                    std::size_t previous_peak_workspace_bytes,
                    topology_write_stats64& stats) -> clipper_error_code {
    stats.peak_workspace_bytes = previous_peak_workspace_bytes;
    topology_layout64 layout;
    layout.polygons = polygon_layouts;
    layout.ring_count = rings.size();
    for (const auto& polygon : polygon_layouts) {
        layout.point_count = checked_size_add(layout.point_count, polygon.point_count);
    }
    layout.maximum_ring_point_count = maximum_ring_point_count;
    layout.staging_workspace_bytes = stats.peak_workspace_bytes;

    writer_transaction transaction{writer};
    transaction.activate();
    if (const auto error = topology_writer64_access::begin(writer, layout);
        error != clipper_error_code::ok) {
        return error;
    }
    for (const auto& descriptor : rings) {
        const topology_ring_layout64 ring{
            descriptor.polygon_index, descriptor.role, descriptor.point_count};
        auto destination = std::span<geotypes::Point2i64>{};
        if (const auto error = topology_writer64_access::acquire(writer, ring, destination);
            error != clipper_error_code::ok) {
            return error;
        }
        if (destination.size() != descriptor.point_count) {
            return clipper_error_code::sink_failure;
        }
        ++stats.output_ring_acquire_count;
        if (!build_path64_into(
                descriptor.record->pts, options.reverse_solution, false, destination)) {
            return clipper_error_code::internal_error;
        }
        stats.output_final_point_writes =
            checked_size_add(stats.output_final_point_writes, destination.size());
    }
    if (const auto error = topology_writer64_access::finish(writer);
        error != clipper_error_code::ok) {
        return error;
    }
    transaction.commit();
    return clipper_error_code::ok;
}


}  // namespace clipper2next::internal
