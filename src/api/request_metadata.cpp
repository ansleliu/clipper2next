#include "clipper2next/clip.h"

#include "clip/engine/private/engine_lifecycle.h"
#include "clip/engine/private/engine_scanline.h"
#include "clip/private/clip_execution_strategy.h"
#include "clip/private/clip_request_metadata.h"
#include "clip/private/clip_request_validation.h"
#include "clip/private/prepared_clip_runtime_data.h"
#include "support/private/checked_size.h"
#include "geometry/private/rectangle_certificate.h"

#include <memory>
#include <utility>

namespace clipper2next {
namespace {

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
        if (const auto rectangle = classify_axis_aligned_rectangle(request.subjects.front())) {
            metadata.single_subject_rect.emplace(rectangle->bounds.left, rectangle->bounds.top,
                rectangle->bounds.right, rectangle->bounds.bottom);
        }
    }
    if (request.clips.size() == 1U) {
        if (const auto rectangle = classify_axis_aligned_rectangle(request.clips.front())) {
            metadata.single_clip_rect.emplace(rectangle->bounds.left, rectangle->bounds.top,
                rectangle->bounds.right, rectangle->bounds.bottom);
        }
    }
    return metadata;
}

}  // namespace internal

auto prepared_clip_request64::operator=(const prepared_clip_request64& other)
    -> prepared_clip_request64& {
    if (this == &other) { return *this; }
    auto staged = prepared_clip_request64{other};
    swap(staged);
    return *this;
}

prepared_clip_request64::prepared_clip_request64(prepared_clip_request64&& other) noexcept
    : request_(std::move(other.request_)),
      metadata_(std::move(other.metadata_)),
      runtime_data_(std::move(other.runtime_data_)) {
    other.reset();
}

auto prepared_clip_request64::operator=(prepared_clip_request64&& other) noexcept
    -> prepared_clip_request64& {
    if (this == &other) { return *this; }
    swap(other);
    other.reset();
    return *this;
}

auto prepared_clip_request64::swap(prepared_clip_request64& other) noexcept -> void {
    using std::swap;
    swap(request_.clip_type, other.request_.clip_type);
    swap(request_.fill_rule, other.request_.fill_rule);
    request_.subjects.swap(other.request_.subjects);
    request_.open_subjects.swap(other.request_.open_subjects);
    request_.clips.swap(other.request_.clips);
    swap(request_.options, other.request_.options);
    swap(metadata_, other.metadata_);
    runtime_data_.swap(other.runtime_data_);
}

auto prepared_clip_request64::reset() noexcept -> void {
    request_.clip_type = ClipType::NoClip;
    request_.fill_rule = FillRule::EvenOdd;
    request_.subjects.clear();
    request_.open_subjects.clear();
    request_.clips.clear();
    request_.options = execution_options{.preserve_collinear = true};
    metadata_ = clip_request_metadata64{};
    runtime_data_.reset();
}

auto prepare_clip_request(clip_request64 request) -> prepared_clip_request64 {
    if (const auto error = internal::validate_clip_request(request);
        error != clipper_error_code::ok) {
        raise_clipper_error(error);
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
