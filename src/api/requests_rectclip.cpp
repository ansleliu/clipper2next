#include "clipper2next/geometry.h"
#include "clipper2next/rectclip/request.h"

#include "rectclip/private/rectclip_path_bounds.h"
#include "rectclip/private/rectclip_facade_runner.h"
#include "rectclip/private/rectclip_thread_state.h"
#include "rectclip/private/rectclip_unprepared_runner.h"

#include <memory>
#include <span>
#include <utility>
#include <vector>

namespace clipper2next {

struct immutable_rect_clip_paths64::runtime_data final {
    Paths64 paths{};
    std::vector<Rect64> path_bounds{};
};

auto rect_clip(const rect_clip_request64& request) -> rect_clip_result64 {
    auto staged =
        internal::execute_rectclip_unprepared_polygons(request.rect, request.paths, false);
    rect_clip_result64 result;
    result.paths = std::move(staged.paths);
    return result;
}

auto rect_clip_checked(const rect_clip_request64& request) -> expected_rect_clip_result64 {
    auto staged = internal::execute_rectclip_unprepared_polygons(request.rect, request.paths, true);
    if (!staged.in_range) {
        return make_clipper_error<rect_clip_result64>(clipper_error_code::coordinate_range);
    }
    rect_clip_result64 result;
    result.paths = std::move(staged.paths);
    return result;
}

auto rect_clip_into(const rect_clip_request64& request, rect_clip_result64& result) -> void {
    result = rect_clip(request);
}

auto prepare_rect_clip_request(rect_clip_request64 request) -> prepared_rect_clip_request64 {
    std::vector<Rect64> path_bounds;
    if (!internal::rectclip_rect_in_range(request.rect) ||
        !internal::build_rectclip_path_bounds_if_in_range(request.paths, path_bounds)) {
        request.paths.clear();
        path_bounds.clear();
    }
    return prepared_rect_clip_request64{std::move(request), std::move(path_bounds)};
}

auto rect_clip(const prepared_rect_clip_request64& request) -> rect_clip_result64 {
    const auto& rect_request = request.request();
    const auto& path_bounds = request.path_bounds_;
    rect_clip_result64 result;
    if (!internal::rectclip_rect_in_range(rect_request.rect) ||
        path_bounds.size() != rect_request.paths.size()) {
        return result;
    }
    if (rect_request.rect.is_empty() || rect_request.paths.empty()) { return result; }
    result.paths = internal::execute_rectclip(rect_request.rect,
                                              rect_request.paths,
                                              path_bounds,
                                              internal::rectclip_mode::polygons);
    return result;
}

auto rect_clip_into(const prepared_rect_clip_request64& request, rect_clip_result64& result)
    -> void {
    result = rect_clip(request);
}

auto prepare_immutable_rect_clip_paths(Paths64 paths) -> immutable_rect_clip_paths64 {
    auto runtime_data = std::make_shared<immutable_rect_clip_paths64::runtime_data>();
    runtime_data->paths = std::move(paths);
    if (!internal::build_rectclip_path_bounds_if_in_range(runtime_data->paths,
                                                          runtime_data->path_bounds)) {
        runtime_data->paths.clear();
        runtime_data->path_bounds.clear();
    }
    return immutable_rect_clip_paths64{std::move(runtime_data)};
}

auto rect_clip(const Rect64& rect, const immutable_rect_clip_paths64& immutable_paths)
    -> rect_clip_result64 {
    const auto& runtime_data_handle = immutable_paths.runtime_data_;
    if (!internal::rectclip_rect_in_range(rect) || rect.is_empty() ||
        runtime_data_handle == nullptr) {
        return {};
    }
    const auto& runtime_data = *runtime_data_handle;
    if (runtime_data.paths.empty() ||
        runtime_data.path_bounds.size() != runtime_data.paths.size()) {
        return {};
    }
    rect_clip_result64 result;
    result.paths = internal::execute_rectclip(
        rect, runtime_data.paths, runtime_data.path_bounds, internal::rectclip_mode::polygons);
    return result;
}

auto rect_clip_into(const Rect64& rect,
                    const immutable_rect_clip_paths64& immutable_paths,
                    rect_clip_result64& result) -> void {
    result = rect_clip(rect, immutable_paths);
}

namespace {

[[nodiscard]] auto rect_clip_lines_impl(const rect_clip_lines_request64& request, bool& in_range)
    -> rect_clip_result64 {
    in_range = true;
    rect_clip_result64 result;
    if (!internal::rectclip_rect_in_range(request.rect)) {
        in_range = false;
        return result;
    }
    if (request.rect.is_empty() || request.lines.empty()) { return result; }
    auto& line_bounds = internal::acquire_reusable_rectclip_bounds_buffer();
    internal::rectclip_path_bounds_summary summary;
    if (!internal::build_rectclip_path_bounds_if_in_range(
            request.lines, line_bounds, summary)) {
        in_range = false;
        return result;
    }
    const auto bounds_view = internal::rectclip_path_bounds_view{
        std::span<const Rect64>{line_bounds},
        summary,
    };
    result.paths = internal::execute_rectclip(request.rect,
                                              request.lines,
                                              bounds_view,
                                              internal::rectclip_mode::lines);
    return result;
}

}  // namespace

auto rect_clip_lines(const rect_clip_lines_request64& request) -> rect_clip_result64 {
    bool in_range = true;
    return rect_clip_lines_impl(request, in_range);
}

auto rect_clip_lines_checked(const rect_clip_lines_request64& request)
    -> expected_rect_clip_result64 {
    bool in_range = true;
    auto result = rect_clip_lines_impl(request, in_range);
    if (!in_range) {
        return make_clipper_error<rect_clip_result64>(clipper_error_code::coordinate_range);
    }
    return result;
}

auto rect_clip_lines_into(const rect_clip_lines_request64& request, rect_clip_result64& result)
    -> void {
    result = rect_clip_lines(request);
}

}  // namespace clipper2next
