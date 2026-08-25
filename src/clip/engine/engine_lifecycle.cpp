#include "clip/engine/private/engine_lifecycle.h"

#include "clip/engine/private/engine_scanline.h"

namespace clipper2next::internal {

auto dispose_active_edges(active_edge_node*& head) noexcept -> void {
    while (head) {
        active_edge_node* next = head->next_in_ael.get();
        head->prev_in_ael = nullptr;
        head->next_in_ael = nullptr;
        head = next;
    }
}

auto dispose_active_edges(active_edge_node_ref& head) noexcept -> void {
    auto* raw_head = head.get();
    dispose_active_edges(raw_head);
    head = raw_head;
}

auto dispose_vertices_and_local_minima(reuseable_data_state& state) noexcept -> void {
    state.minima_list_.clear();
    state.vertex_lists_.clear();
}

auto dispose_vertices_and_local_minima(clipper_base_state& state) noexcept -> void {
    state.minima_list_.clear();
    state.vertex_lists_.clear();
}

auto cleanup_engine_state(clipper_base_state& state) noexcept -> void {
    state.actives_ = nullptr;
    state.sel_ = nullptr;
    state.active_pool_.clear();
    state.scanline_list_.clear();
    state.intersect_nodes_.clear();
    state.scanbeam_top_edges_.clear();
    state.scanbeam_unit_edges_.clear();
    state.scanbeam_unit_scratch_.clear();
    state.output_owner_.dispose_all();
    state.horz_seg_list_.clear();
    state.horz_join_list_.clear();
    state.precomputed_scanline_heap_ = {};
}

auto reset_engine_state_for_reuse(clipper_base_state& state) noexcept -> void {
    cleanup_engine_state(state);
    dispose_vertices_and_local_minima(state);
    state.minima_list_sorted_ = false;
    state.precomputed_scanline_heap_ = {};
    state.cliptype_ = ClipType::NoClip;
    state.fillrule_ = FillRule::EvenOdd;
    state.fillpos = FillRule::Positive;
}

auto release_engine_state_storage(clipper_base_state& state) noexcept -> void {
    cleanup_engine_state(state);
    dispose_vertices_and_local_minima(state);
    LocalMinimaList{}.swap(state.minima_list_);
    state.vertex_lists_.release();
    state.active_pool_.release();
    state.scanline_list_.release();
    IntersectNodeList{}.swap(state.intersect_nodes_);
    decltype(state.scanbeam_top_edges_){}.swap(state.scanbeam_top_edges_);
    decltype(state.scanbeam_unit_edges_){}.swap(state.scanbeam_unit_edges_);
    decltype(state.scanbeam_unit_scratch_){}.swap(state.scanbeam_unit_scratch_);
    HorzSegmentList{}.swap(state.horz_seg_list_);
    decltype(state.horz_join_list_){}.swap(state.horz_join_list_);
    state.output_owner_.release();
}

auto reset_engine_state(clipper_base_state& state) -> void {
    if (!state.minima_list_sorted_) {
        sort_local_minima(state.minima_list_);
        state.minima_list_sorted_ = true;
    }

    // Pre-size hot working buffers once per execute() call to reduce growth churn
    // in scanbeam-heavy workloads (many small open paths).
    const auto minima_count = state.minima_list_.size();
    if (state.intersect_nodes_.capacity() < minima_count) {
        state.intersect_nodes_.reserve(minima_count);
    }
    if (state.horz_seg_list_.capacity() < minima_count) {
        state.horz_seg_list_.reserve(minima_count);
    }
    if (state.horz_join_list_.capacity() < minima_count) {
        state.horz_join_list_.reserve(minima_count);
    }

    if (!state.precomputed_scanline_heap_.empty()) {
        reset_scanlines(state.scanline_list_, state.precomputed_scanline_heap_);
    } else {
        reset_scanlines(state.scanline_list_, state.minima_list_);
    }

    state.current_locmin_iter_ = state.minima_list_.begin();
    state.actives_ = nullptr;
    state.sel_ = nullptr;
}

}  // namespace clipper2next::internal
