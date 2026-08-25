#pragma once

#include "clipper2next/core.h"
#include "clip/engine/private/engine_output_owner.h"
#include "clip/engine/private/engine_scanline.h"
#include "support/private/storage/stable_pool.h"
#include "clip/engine/private/engine_types.h"

#include <span>
#include <vector>

namespace clipper2next {

namespace internal {

struct reuseable_data_state {
    LocalMinimaList minima_list_;
    VertexStorageList vertex_lists_;
};

struct clipper_base_state {
    ClipType cliptype_ = ClipType::NoClip;
    FillRule fillrule_ = FillRule::EvenOdd;
    FillRule fillpos = FillRule::Positive;

    [[nodiscard]] auto uses_positive_fill_rule() const noexcept -> bool {
        return fillpos == FillRule::Positive;
    }

    int64_t bot_y_ = 0;
    bool minima_list_sorted_ = false;
    bool using_polytree_ = false;
    std::span<const int64_t> precomputed_scanline_heap_{};

    [[nodiscard]] auto has_active_edges() const noexcept -> bool { return actives_ != nullptr; }

    active_edge_node_ref actives_;
    active_edge_node_ref sel_;
    stable_pool<active_edge_node> active_pool_;
    LocalMinimaList minima_list_;
    LocalMinimaList::iterator current_locmin_iter_;
    VertexStorageList vertex_lists_;
    scanline_queue scanline_list_;
    IntersectNodeList intersect_nodes_;
    std::vector<active_edge_node*> scanbeam_top_edges_;
    std::vector<active_edge_node*> scanbeam_unit_edges_;
    std::vector<active_edge_node*> scanbeam_unit_scratch_;
    HorzSegmentList horz_seg_list_;
    std::vector<horizontal_join_node> horz_join_list_;
    engine_output_owner output_owner_;
};

}  // namespace internal
}  // namespace clipper2next
