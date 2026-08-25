// SPDX-License-Identifier: BSL-1.0

#include "clip/engine/private/engine_graph.h"

#include "clipper2next/api/error.h"

#include <limits>

namespace clipper2next::internal {

auto engine_graph::next_generation() const noexcept -> std::uint32_t {
    return generation_ == 0 ? 1 : generation_;
}

auto engine_graph::create_edge(Point64 bottom, Point64 top_point) -> edge_id {
    const auto id = edge_id{
        static_cast<std::uint32_t>(edges_.size()),
        next_generation(),
    };
    edges_.push_back(
        slot<engine_edge_record>{id.generation, engine_edge_record{bottom, top_point}});
    return id;
}

auto engine_graph::create_output_record() -> output_record_id {
    const auto id = output_record_id{
        static_cast<std::uint32_t>(output_records_.size()),
        next_generation(),
    };
    output_records_.push_back(slot<output_record>{id.generation, output_record{}});
    return id;
}

auto engine_graph::append_output_point(output_record_id record_id, Point64 point)
    -> output_point_id {
    auto& record = output_record_ref(record_id);
    const auto id = output_point_id{
        static_cast<std::uint32_t>(output_points_.size()),
        next_generation(),
    };
    auto node = output_point_record{point, id, id};
    output_points_.push_back(slot<output_point_record>{id.generation, node});

    if (!record.first) {
        record.first = id;
        return id;
    }

    auto& first = output_point(record.first);
    auto& tail = output_point(first.previous);
    auto& inserted = output_point(id);
    inserted.next = record.first;
    inserted.previous = first.previous;
    tail.next = id;
    first.previous = id;
    return id;
}

auto engine_graph::contains(edge_id id) const noexcept -> bool {
    return id.index < edges_.size() && edges_[id.index].generation == id.generation;
}

auto engine_graph::contains(output_record_id id) const noexcept -> bool {
    return id.index < output_records_.size() &&
           output_records_[id.index].generation == id.generation;
}

auto engine_graph::contains(output_point_id id) const noexcept -> bool {
    return id.index < output_points_.size() && output_points_[id.index].generation == id.generation;
}

auto engine_graph::edge(edge_id id) -> engine_edge_record& {
    if (!contains(id)) { throw clipper_error(clipper_error_code::internal_error); }
    return edges_[id.index].value;
}

auto engine_graph::edge(edge_id id) const -> const engine_edge_record& {
    if (!contains(id)) { throw clipper_error(clipper_error_code::internal_error); }
    return edges_[id.index].value;
}

auto engine_graph::output_ring(output_record_id id) const -> std::vector<Point64> {
    const auto& record = output_record_ref(id);
    std::vector<Point64> points;
    if (!record.first) { return points; }

    auto current = record.first;
    do {
        const auto& node = output_point(current);
        points.push_back(node.point);
        current = node.next;
    } while (current != record.first);
    return points;
}

auto engine_graph::clear() noexcept -> void {
    edges_.clear();
    output_points_.clear();
    output_records_.clear();
    ++generation_;
    if (generation_ == 0) { generation_ = 1; }
}

auto engine_graph::output_point(output_point_id id) -> output_point_record& {
    if (!contains(id)) { throw clipper_error(clipper_error_code::internal_error); }
    return output_points_[id.index].value;
}

auto engine_graph::output_point(output_point_id id) const -> const output_point_record& {
    if (!contains(id)) { throw clipper_error(clipper_error_code::internal_error); }
    return output_points_[id.index].value;
}

auto engine_graph::output_record_ref(output_record_id id) -> output_record& {
    if (!contains(id)) { throw clipper_error(clipper_error_code::internal_error); }
    return output_records_[id.index].value;
}

auto engine_graph::output_record_ref(output_record_id id) const -> const output_record& {
    if (!contains(id)) { throw clipper_error(clipper_error_code::internal_error); }
    return output_records_[id.index].value;
}

}  // namespace clipper2next::internal
