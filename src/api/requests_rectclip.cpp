#include "clipper2next/geometry.h"
#include "api/private/checked_execution.h"
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

auto prepared_rect_clip_request64::operator=(const prepared_rect_clip_request64& other)
    -> prepared_rect_clip_request64& {
    if (this == &other) { return *this; }
    auto staged = prepared_rect_clip_request64{other};
    swap(staged);
    return *this;
}

prepared_rect_clip_request64::prepared_rect_clip_request64(
    prepared_rect_clip_request64&& other) noexcept
    : request_(std::move(other.request_)), path_bounds_(std::move(other.path_bounds_)) {
    other.reset();
}

auto prepared_rect_clip_request64::operator=(prepared_rect_clip_request64&& other) noexcept
    -> prepared_rect_clip_request64& {
    if (this == &other) { return *this; }
    swap(other);
    other.reset();
    return *this;
}

auto prepared_rect_clip_request64::swap(prepared_rect_clip_request64& other) noexcept -> void {
    using std::swap;
    swap(request_.rect, other.request_.rect);
    request_.paths.swap(other.request_.paths);
    path_bounds_.swap(other.path_bounds_);
}

auto prepared_rect_clip_request64::reset() noexcept -> void {
    request_.rect = Rect64{};
    request_.paths.clear();
    path_bounds_.clear();
}

auto rect_clip(const rect_clip_request64& request) -> rect_clip_result64 {
    return {internal::execute_rectclip_unprepared_polygons(
        request.rect, request.paths, false).paths};
}

auto rect_clip_checked(const rect_clip_request64& request) -> expected_rect_clip_result64 {
    return internal::checked_call<rect_clip_result64>([&]() -> clipper_result<rect_clip_result64> {
        auto completed = internal::execute_rectclip_unprepared_polygons(
            request.rect, request.paths, true);
        if (!completed.in_range) {
            return make_clipper_error<rect_clip_result64>(clipper_error_code::coordinate_range);
        }
        return rect_clip_result64{std::move(completed.paths)};
    });
}

auto rect_clip_into(const rect_clip_request64& request, rect_clip_result64& result) -> void {
    static_cast<void>(internal::execute_rectclip_unprepared_polygons_into(
        request.rect, request.paths, false, result.paths));
}

auto prepare_rect_clip_request(rect_clip_request64 request) -> prepared_rect_clip_request64 {
    std::vector<Rect64> path_bounds;
    if (!internal::rectclip_rect_in_range(request.rect) ||
        !internal::build_rectclip_path_bounds(request.paths, true, path_bounds)) {
        raise_clipper_error(clipper_error_code::coordinate_range);
    }
    return prepared_rect_clip_request64{std::move(request), std::move(path_bounds)};
}

auto rect_clip(const prepared_rect_clip_request64& request) -> rect_clip_result64 {
    const auto& input = request.request();
    if (input.rect.is_empty()) { return {}; }
    internal::rectclip_operation_lease operation;
    return {internal::build_rectclip_paths(operation.context(input.rect), input.paths, request.path_bounds_,
                                         internal::rectclip_mode::polygons)};
}

auto rect_clip_into(const prepared_rect_clip_request64& request, rect_clip_result64& result)
    -> void {
    const auto& input = request.request();
    if (input.rect.is_empty()) { result.paths.clear(); return; }
    internal::rectclip_operation_lease operation;
    internal::execute_rectclip_into(operation.context(input.rect),
                                    input.paths,
                                    request.path_bounds_,
                                    internal::rectclip_mode::polygons,
                                    result.paths);
}

auto prepare_immutable_rect_clip_paths(Paths64 paths) -> immutable_rect_clip_paths64 {
    std::vector<Rect64> path_bounds;
    if (!internal::build_rectclip_path_bounds(paths, true, path_bounds)) {
        raise_clipper_error(clipper_error_code::coordinate_range);
    }
    auto runtime_data = std::make_shared<immutable_rect_clip_paths64::runtime_data>();
    runtime_data->paths = std::move(paths);
    runtime_data->path_bounds = std::move(path_bounds);
    return immutable_rect_clip_paths64{std::move(runtime_data)};
}

auto rect_clip(const Rect64& rect, const immutable_rect_clip_paths64& immutable_paths)
    -> rect_clip_result64 {
    if (!internal::rectclip_rect_in_range(rect)) { return {}; }
    const auto& data = immutable_paths.runtime_data_;
    if (!data) { return {}; }
    if (rect.is_empty()) { return {}; }
    internal::rectclip_operation_lease operation;
    return {internal::build_rectclip_paths(
        operation.context(rect), data->paths, data->path_bounds, internal::rectclip_mode::polygons)};
}

auto rect_clip_into(const Rect64& rect,
                    const immutable_rect_clip_paths64& immutable_paths,
                    rect_clip_result64& result) -> void {
    if (!internal::rectclip_rect_in_range(rect)) { return; }
    const auto& data = immutable_paths.runtime_data_;
    if (!data) {
        result.paths.clear();
        return;
    }
    if (rect.is_empty()) { result.paths.clear(); return; }
    internal::rectclip_operation_lease operation;
    internal::execute_rectclip_into(
        operation.context(rect), data->paths, data->path_bounds, internal::rectclip_mode::polygons, result.paths);
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
    if (request.lines.empty()) { return result; }
    internal::rectclip_operation_lease operation;
    auto& line_bounds = operation.bounds();
    if (!internal::build_rectclip_path_bounds(request.lines, true, line_bounds)) {
        in_range = false;
        return result;
    }
    if (request.rect.is_empty()) { return result; }
    result.paths = internal::build_rectclip_line_paths(request.rect, request.lines, line_bounds);
    return result;
}

}  // namespace

auto rect_clip_lines(const rect_clip_lines_request64& request) -> rect_clip_result64 {
    bool in_range = true;
    return rect_clip_lines_impl(request, in_range);
}

auto rect_clip_lines_checked(const rect_clip_lines_request64& request)
    -> expected_rect_clip_result64 {
    return internal::checked_call<rect_clip_result64>([&]() -> clipper_result<rect_clip_result64> {
        bool in_range = true;
        auto result = rect_clip_lines_impl(request, in_range);
        if (!in_range) {
            return make_clipper_error<rect_clip_result64>(clipper_error_code::coordinate_range);
        }
        return result;
    });
}

auto rect_clip_lines_into(const rect_clip_lines_request64& request, rect_clip_result64& result)
    -> void {
    if (!internal::rectclip_rect_in_range(request.rect)) { return; }
    internal::rectclip_operation_lease operation;
    auto& bounds = operation.bounds();
    if (!internal::build_rectclip_path_bounds(request.lines, true, bounds)) { return; }
    if (request.rect.is_empty()) { result.paths.clear(); return; }
    internal::execute_rectclip_lines_into(request.rect, request.lines, bounds, result.paths);
}

}  // namespace clipper2next
