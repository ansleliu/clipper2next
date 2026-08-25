#include "clip/engine/private/engine_scanline.h"

#include <algorithm>

namespace clipper2next::internal {

auto scanline_queue::reserve(std::size_t capacity) -> void {
    values_.reserve(capacity);
}

auto scanline_queue::clear() noexcept -> void {
    values_.clear();
}

auto scanline_queue::release() noexcept -> void {
    std::vector<int64_t>{}.swap(values_);
}

auto scanline_queue::empty() const noexcept -> bool {
    return values_.empty();
}

auto scanline_queue::size() const noexcept -> std::size_t {
    return values_.size();
}

auto scanline_queue::capacity() const noexcept -> std::size_t {
    return values_.capacity();
}

auto push_scanline(scanline_queue& scanlines, int64_t y) -> void {
    scanlines.values_.push_back(y);
    std::push_heap(scanlines.values_.begin(), scanlines.values_.end());
}

auto pop_scanline(scanline_queue& scanlines, int64_t& y) -> bool {
    auto& values = scanlines.values_;
    if (values.empty()) { return false; }
    std::pop_heap(values.begin(), values.end());
    y = values.back();
    values.pop_back();
    while (!values.empty() && y == values.front()) {
        std::pop_heap(values.begin(), values.end());
        values.pop_back();
    }
    return true;
}

auto reset_scanlines(scanline_queue& scanlines, const LocalMinimaList& minima) -> void {
    scanlines.values_.clear();
    scanlines.values_.reserve(minima.size());
    bool has_last_y = false;
    int64_t last_y = 0;
    // Minima are sorted bottom-up, so unique y values are already a max-heap.
    for (auto iterator = minima.begin(); iterator != minima.end(); ++iterator) {
        const auto y = iterator->vertex.get().pt.y;
        if (has_last_y && y == last_y) { continue; }
        scanlines.values_.emplace_back(y);
        has_last_y = true;
        last_y = y;
    }
}

auto reset_scanlines(scanline_queue& scanlines, std::span<const int64_t> precomputed_heap) -> void {
    scanlines.values_.assign(precomputed_heap.begin(), precomputed_heap.end());
}

auto pop_local_minima(LocalMinimaList::iterator& current,
                      const LocalMinimaList::iterator& end,
                      int64_t y,
                      local_minimum_node*& local_minima) -> bool {
    if (current == end || current->vertex.get().pt.y != y) { return false; }
    local_minima = &*(current++);
    return true;
}

auto compare_local_minima_bottom_up(const local_minimum_node& first,
                                    const local_minimum_node& second) -> bool {
    const auto& first_pt = first.vertex.get().pt;
    const auto& second_pt = second.vertex.get().pt;
    return (first_pt.y == second_pt.y) ? (first_pt.x < second_pt.x) : (first_pt.y > second_pt.y);
}

auto sort_local_minima(LocalMinimaList& minima) -> void {
    std::stable_sort(minima.begin(), minima.end(), compare_local_minima_bottom_up);
}

}  // namespace clipper2next::internal
