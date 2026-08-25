#pragma once

#include "clip/engine/private/engine_types.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace clipper2next::internal {

class scanline_queue final {
public:
    auto reserve(std::size_t capacity) -> void;
    auto clear() noexcept -> void;
    // Unlike clear(), also returns the heap storage to the allocator.
    auto release() noexcept -> void;

    [[nodiscard]] auto empty() const noexcept -> bool;
    [[nodiscard]] auto size() const noexcept -> std::size_t;
    [[nodiscard]] auto capacity() const noexcept -> std::size_t;

private:
    friend auto push_scanline(scanline_queue& scanlines, int64_t y) -> void;
    friend auto pop_scanline(scanline_queue& scanlines, int64_t& y) -> bool;
    friend auto reset_scanlines(scanline_queue& scanlines, const LocalMinimaList& minima) -> void;
    friend auto reset_scanlines(scanline_queue& scanlines,
                                std::span<const int64_t> precomputed_heap) -> void;

    std::vector<int64_t> values_;
};

auto push_scanline(scanline_queue& scanlines, int64_t y) -> void;
[[nodiscard]] auto pop_scanline(scanline_queue& scanlines, int64_t& y) -> bool;
auto reset_scanlines(scanline_queue& scanlines, const LocalMinimaList& minima) -> void;
auto reset_scanlines(scanline_queue& scanlines, std::span<const int64_t> precomputed_heap) -> void;

[[nodiscard]] auto pop_local_minima(LocalMinimaList::iterator& current,
                                    const LocalMinimaList::iterator& end,
                                    int64_t y,
                                    local_minimum_node*& local_minima) -> bool;

[[nodiscard]] auto compare_local_minima_bottom_up(const local_minimum_node& first,
                                                  const local_minimum_node& second) -> bool;

auto sort_local_minima(LocalMinimaList& minima) -> void;

}  // namespace clipper2next::internal
