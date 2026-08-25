#pragma once

#include "clipper2next/clip/types.h"
#include "support/private/storage/topology_store.h"
#include "clipper2next/polygon/poly_tree.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace clipper2next {

enum class JoinWith { NoJoin, Left, Right };

namespace internal {
class engine_output_owner;
}

namespace engine_private {

struct Scanline;
struct IntersectNode;
struct active_edge_node;
struct Vertex;
struct local_minimum_node;
struct output_point_node;
struct output_record_node;
struct horizontal_segment_node;
struct vertex_tag;
struct output_point_node_tag;
struct output_record_node_tag;
struct active_edge_node_tag;
struct local_minimum_node_tag;

using vertex_ref = internal::topology_ref<Vertex, vertex_tag>;
using output_point_node_ref = internal::topology_ref<output_point_node, output_point_node_tag>;
using output_record_node_ref = internal::topology_ref<output_record_node, output_record_node_tag>;
using active_edge_node_ref = internal::topology_ref<active_edge_node, active_edge_node_tag>;
using local_minimum_node_ref = internal::topology_ref<local_minimum_node, local_minimum_node_tag>;

enum class VertexFlags : uint32_t {
    Empty = 0,
    OpenStart = 1,
    OpenEnd = 2,
    LocalMax = 4,
    LocalMin = 8
};

[[nodiscard]] constexpr auto vertex_flag_bits(VertexFlags flags) -> uint32_t {
    return static_cast<uint32_t>(flags);
}

constexpr auto operator&(VertexFlags first, VertexFlags second) -> VertexFlags {
    return static_cast<VertexFlags>(vertex_flag_bits(first) & vertex_flag_bits(second));
}

constexpr auto operator|(VertexFlags first, VertexFlags second) -> VertexFlags {
    return static_cast<VertexFlags>(vertex_flag_bits(first) | vertex_flag_bits(second));
}

struct Vertex {
    using point_type = Point64;

    Point64 pt{};
    vertex_ref next{};
    vertex_ref prev{};

    Vertex() = default;
    explicit Vertex(Point64 point) noexcept
        : pt(point) {}

    [[nodiscard]] auto is_local_minimum() const -> bool {
        return (flags & VertexFlags::LocalMin) != VertexFlags::Empty;
    }

    VertexFlags flags = VertexFlags::Empty;
};

struct output_point_node {
    using owning_record = output_record_node;

    Point64 pt;
    output_point_node_ref next;
    output_point_node_ref prev;
    bool has_horizontal_segment = false;
    output_record_node_ref outrec;

    [[nodiscard]] auto is_isolated_ring() const -> bool { return next == this && prev == this; }

    auto link_to_self() noexcept -> void {
        next = this;
        prev = this;
    }

    output_point_node(const Point64& pt_, output_record_node& outrec_)
        : pt(pt_),
          outrec(outrec_) {
        link_to_self();
    }
};

using OutRecList = std::vector<output_record_node_ref>;

struct output_record_node {
    size_t idx = 0;
    internal::engine_output_owner* output_owner = nullptr;

    [[nodiscard]] auto has_points() const -> bool { return pts != nullptr; }

    output_record_node_ref owner;
    active_edge_node_ref front_edge;
    active_edge_node_ref back_edge;
    output_point_node_ref pts;
    std::optional<std::size_t> polygon_node;
    OutRecList splits;

    [[nodiscard]] auto has_split_records() const -> bool { return !splits.empty(); }

    output_record_node_ref recursive_split;
    Rect64 bounds = {};
    Path64 path;
    bool is_open = false;
};

struct active_edge_node {
    using coordinate_type = int64_t;

    Point64 bottom;
    Point64 top_point;
    int64_t current_x = 0;
    int64_t scanbeam_top_x = 0;
    double dx = 0.0;

    [[nodiscard]] auto is_horizontal() const -> bool { return bottom.y == top_point.y; }

    int wind_dx = 1;
    int winding_count = 0;
    int wind_cnt2 = 0;
    output_record_node_ref outrec;
    active_edge_node_ref prev_in_ael;
    active_edge_node_ref next_in_ael;

    [[nodiscard]] auto has_output() const -> bool { return outrec != nullptr; }

    active_edge_node_ref prev_in_sel;
    active_edge_node_ref next_in_sel;

    [[nodiscard]] auto has_selected_neighbor() const -> bool {
        return prev_in_sel != nullptr || next_in_sel != nullptr;
    }

    active_edge_node_ref jump;
    vertex_ref vertex_top;
    local_minimum_node_ref local_min;
    bool is_left_bound = false;
    JoinWith join_with = JoinWith::NoJoin;

    [[nodiscard]] auto is_joined() const -> bool { return join_with != JoinWith::NoJoin; }
};

struct local_minimum_node {
    std::reference_wrapper<Vertex> vertex;
    PathType polytype;

    [[nodiscard]] auto is_subject() const -> bool { return polytype == PathType::Subject; }

    bool is_open;

    local_minimum_node(Vertex& v, PathType pt, bool open)
        : vertex(v),
          polytype(pt),
          is_open(open) {}

    [[nodiscard]] auto is_open_path() const -> bool { return is_open; }
};

struct IntersectNode {
    Point64 pt{};
    active_edge_node_ref edge1;
    active_edge_node_ref edge2;

    IntersectNode() = default;

    static auto from_point(const Point64& point) noexcept -> IntersectNode {
        IntersectNode node;
        node.pt = point;
        return node;
    }

    IntersectNode(active_edge_node& first, active_edge_node& second, const Point64& pt_) noexcept
        : pt(pt_),
          edge1(first),
          edge2(second) {}

    [[nodiscard]] auto first_edge() noexcept -> active_edge_node& { return *edge1; }

    [[nodiscard]] auto first_edge() const noexcept -> const active_edge_node& { return *edge1; }

    [[nodiscard]] auto second_edge() noexcept -> active_edge_node& { return *edge2; }

    [[nodiscard]] auto second_edge() const noexcept -> const active_edge_node& { return *edge2; }

    [[nodiscard]] auto has_edges() const -> bool { return edge1 != nullptr && edge2 != nullptr; }
};

struct horizontal_segment_node {
    std::optional<std::reference_wrapper<output_point_node>> left_op;
    std::optional<std::reference_wrapper<output_point_node>> right_op;
    bool left_to_right = true;

    horizontal_segment_node() = default;
    explicit horizontal_segment_node(output_point_node& op) noexcept
        : left_op(op) {}

    [[nodiscard]] auto left_point() noexcept -> output_point_node& { return (*left_op).get(); }

    [[nodiscard]] auto left_point() const noexcept -> const output_point_node& {
        return (*left_op).get();
    }

    [[nodiscard]] auto right_point() noexcept -> output_point_node& { return (*right_op).get(); }

    [[nodiscard]] auto right_point() const noexcept -> const output_point_node& {
        return (*right_op).get();
    }

    [[nodiscard]] auto has_right_point() const noexcept -> bool { return right_op.has_value(); }

    auto set_left_point(output_point_node& point) noexcept -> void { left_op = point; }

    auto set_right_point(output_point_node& point) noexcept -> void { right_op = point; }

    auto clear_right_point() noexcept -> void { right_op.reset(); }

    [[nodiscard]] auto is_complete() const -> bool {
        return left_op.has_value() && right_op.has_value();
    }
};

struct horizontal_join_node {
    std::optional<std::reference_wrapper<output_point_node>> op1;
    std::optional<std::reference_wrapper<output_point_node>> op2;

    horizontal_join_node() = default;
    explicit horizontal_join_node(output_point_node& ltr, output_point_node& rtl) noexcept
        : op1(ltr),
          op2(rtl) {}

    [[nodiscard]] auto first_point() noexcept -> output_point_node& { return (*op1).get(); }

    [[nodiscard]] auto first_point() const noexcept -> const output_point_node& {
        return (*op1).get();
    }

    [[nodiscard]] auto second_point() noexcept -> output_point_node& { return (*op2).get(); }

    [[nodiscard]] auto second_point() const noexcept -> const output_point_node& {
        return (*op2).get();
    }

    [[nodiscard]] auto has_pair() const -> bool { return op1.has_value() && op2.has_value(); }
};

using HorzSegmentList = std::vector<horizontal_segment_node>;
using LocalMinima_ptr = std::unique_ptr<local_minimum_node>;
// Local minima are stored by value in a contiguous vector: it avoids one small
// heap allocation per minimum and keeps them cache-local. Edges reference a
// minimum by raw pointer (active_edge_node::local_min); those pointers are taken
// only during execute(), after the list is fully built and sorted and while it
// no longer grows, so element addresses are stable when referenced.
using LocalMinimaList = std::vector<local_minimum_node>;
using IntersectNodeList = std::vector<IntersectNode>;

// Request parsing stores vertices in reusable blocks so a long-lived engine
// state can amortize allocation growth across execute() calls.
class VertexStorageList final {
public:
    auto clear() noexcept -> void { used_blocks_ = 0; }

    auto release() noexcept -> void {
        blocks_.clear();
        used_blocks_ = 0;
    }

    [[nodiscard]] auto size() const noexcept -> std::size_t { return used_blocks_; }

    auto reserve(std::size_t block_count) -> void { blocks_.reserve(block_count); }

    [[nodiscard]] auto acquire(std::size_t vertex_count) -> Vertex* {
        if (used_blocks_ == blocks_.size()) { blocks_.emplace_back(); }
        auto& block = blocks_[used_blocks_++];
        block.vertices.resize(vertex_count);
        return block.vertices.data();
    }

private:
    struct vertex_block final {
        std::vector<Vertex> vertices;
    };

    std::vector<vertex_block> blocks_;
    std::size_t used_blocks_ = 0;
};

}  // namespace engine_private

namespace internal {

using active_edge_node = engine_private::active_edge_node;
using active_edge_node_ref = engine_private::active_edge_node_ref;
using horizontal_join_node = engine_private::horizontal_join_node;
using horizontal_segment_node = engine_private::horizontal_segment_node;
using HorzSegmentList = engine_private::HorzSegmentList;
using IntersectNode = engine_private::IntersectNode;
using IntersectNodeList = engine_private::IntersectNodeList;
using local_minimum_node = engine_private::local_minimum_node;
using local_minimum_node_ref = engine_private::local_minimum_node_ref;
using LocalMinimaList = engine_private::LocalMinimaList;
using LocalMinima_ptr = engine_private::LocalMinima_ptr;
using output_point_node = engine_private::output_point_node;
using output_point_node_ref = engine_private::output_point_node_ref;
using output_record_node = engine_private::output_record_node;
using output_record_node_ref = engine_private::output_record_node_ref;
using OutRecList = engine_private::OutRecList;
using Vertex = engine_private::Vertex;
using vertex_ref = engine_private::vertex_ref;
using VertexFlags = engine_private::VertexFlags;
using VertexStorageList = engine_private::VertexStorageList;

}  // namespace internal
}  // namespace clipper2next
