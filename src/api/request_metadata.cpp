#include "clipper2next/clip.h"

#include "clip/engine/private/engine_lifecycle.h"
#include "clip/engine/private/engine_scanline.h"
#include "clip/private/clip_execution_strategy.h"
#include "clip/private/clip_request_metadata.h"
#include "clip/private/clip_request_validation.h"
#include "clip/private/prepared_clip_runtime_data.h"
#include "support/private/checked_size.h"

#include <algorithm>
#include <array>
#include <memory>
#include <optional>
#include <utility>

namespace clipper2next {
namespace {

[[nodiscard]] auto path_as_axis_aligned_rect(const Path64& path) -> std::optional<Rect64> {
    if (path.size() != 4U) { return std::nullopt; }

    std::array<int64_t, 4U> xs{};
    std::array<int64_t, 4U> ys{};
    for (std::size_t index = 0; index < path.size(); ++index) {
        xs[index] = path[index].x;
        ys[index] = path[index].y;
    }

    const auto [min_x, max_x] = std::minmax_element(xs.begin(), xs.end());
    const auto [min_y, max_y] = std::minmax_element(ys.begin(), ys.end());
    Rect64 rect{*min_x, *min_y, *max_x, *max_y};
    if (rect.is_empty()) { return std::nullopt; }

    const std::array<Point64, 4U> corners{
        Point64{rect.left, rect.top},
        Point64{rect.right, rect.top},
        Point64{rect.right, rect.bottom},
        Point64{rect.left, rect.bottom},
    };
    for (const auto& corner : corners) {
        if (std::find(path.begin(), path.end(), corner) == path.end()) { return std::nullopt; }
    }

    // Containing all four corners is not enough: a diagonal ordering such as
    // (TL, BR, TR, BL) is a self-intersecting bowtie, not a rectangle. Require
    // every edge to be axis-aligned (consecutive points share exactly one axis).
    for (std::size_t index = 0; index < path.size(); ++index) {
        const auto& current = path[index];
        const auto& next = path[(index + 1U) % path.size()];
        if (current.x != next.x && current.y != next.y) { return std::nullopt; }
    }
    return rect;
}

[[nodiscard]] auto point_count(const Paths64& paths) -> std::size_t {
    std::size_t count = 0;
    for (const auto& path : paths) { count = internal::checked_size_add(count, path.size()); }
    return count;
}

}  // namespace

namespace internal {

auto build_clip_request_metadata(const clip_request64& request) -> clip_request_metadata64 {
    clip_request_metadata64 metadata;
    metadata.subject_path_count = request.subjects.size();
    metadata.open_subject_path_count = request.open_subjects.size();
    metadata.clip_path_count = request.clips.size();
    metadata.subject_point_count = point_count(request.subjects);
    metadata.open_subject_point_count = point_count(request.open_subjects);
    metadata.clip_point_count = point_count(request.clips);
    if (request.subjects.size() == 1U) {
        metadata.single_subject_rect = path_as_axis_aligned_rect(request.subjects.front());
    }
    if (request.clips.size() == 1U) {
        metadata.single_clip_rect = path_as_axis_aligned_rect(request.clips.front());
    }
    return metadata;
}

}  // namespace internal

auto prepare_clip_request(clip_request64 request) -> prepared_clip_request64 {
    if (!internal::clip_request_in_range(request)) {
        return prepared_clip_request64{std::move(request), clip_request_metadata64{}, {}};
    }

    auto metadata = internal::build_clip_request_metadata(request);

    auto runtime_data = std::make_shared<prepared_clip_request64::runtime_data>();
    internal::add_paths_to_state(
        runtime_data->reusable_state, request.subjects, PathType::Subject, false);
    internal::add_paths_to_state(
        runtime_data->reusable_state, request.open_subjects, PathType::Subject, true);
    internal::add_paths_to_state(
        runtime_data->reusable_state, request.clips, PathType::Clip, false);
    internal::sort_local_minima(runtime_data->reusable_state.minima_list_);
    runtime_data->minima_sorted = true;
    runtime_data->scanline_heap.reserve(runtime_data->reusable_state.minima_list_.size());
    bool has_last_y = false;
    int64_t last_y = 0;
    // Minima are sorted bottom-up, so unique y values are already a max-heap.
    for (auto iterator = runtime_data->reusable_state.minima_list_.begin();
         iterator != runtime_data->reusable_state.minima_list_.end();
         ++iterator) {
        const int64_t y = iterator->vertex.get().pt.y;
        if (has_last_y && y == last_y) { continue; }
        runtime_data->scanline_heap.push_back(y);
        has_last_y = true;
        last_y = y;
    }

    const bool can_cache_exact_result =
        request.clip_type == ClipType::Intersection && request.fill_rule == FillRule::NonZero &&
        request.subjects.empty() && !request.open_subjects.empty() && request.clips.size() == 1U &&
        request.options.preserve_collinear &&
        !request.options.reverse_solution;
    if (can_cache_exact_result) { runtime_data->cached_result = internal::execute_clip_validated(request); }

    return prepared_clip_request64{
        std::move(request), std::move(metadata), std::move(runtime_data)};
}

}  // namespace clipper2next
