#include "clipper2next/offset/operations.h"

#include "clip/private/clip_request_validation.h"
#include "offset/private/offset_algorithm.h"
#include "offset/private/offset_group.h"
#include "offset/private/offset_thread_state.h"

#include <cmath>
#include <utility>
#include <vector>

namespace clipper2next {
namespace {

[[nodiscard]] auto paths_in_range(const Paths64& paths) -> bool {
    for (const auto& path : paths) {
        for (const auto& point : path) {
            if (!internal::clip_coordinate_in_range(point.x) ||
                !internal::clip_coordinate_in_range(point.y)) {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] auto copy_paths_if_in_range(const Paths64& paths, Paths64& result) -> bool {
    result.clear();
    result.reserve(paths.size());
    for (const auto& path : paths) {
        auto& copied = result.emplace_back();
        copied.reserve(path.size());
        for (const auto& point : path) {
            if (!internal::clip_coordinate_in_range(point.x) ||
                !internal::clip_coordinate_in_range(point.y)) {
                result.clear();
                return false;
            }
            copied.emplace_back(point);
        }
    }
    return true;
}

[[nodiscard]] auto copy_clean_path(const Path64& source,
                                   bool is_closed,
                                   bool check_coordinate_range,
                                   Path64& destination) -> bool {
    destination.clear();
    bool needs_cleanup = false;
    const Point64* previous = nullptr;
    for (const auto& point : source) {
        if (check_coordinate_range &&
            (!internal::clip_coordinate_in_range(point.x) ||
             !internal::clip_coordinate_in_range(point.y))) {
            return false;
        }
        if (previous != nullptr && *previous == point) { needs_cleanup = true; }
        previous = &point;
    }
    needs_cleanup = needs_cleanup ||
                    (is_closed && source.size() > 1U && source.back() == source.front());
    if (!needs_cleanup) {
        destination.assign(source.begin(), source.end());
        return true;
    }

    destination.reserve(source.size());
    for (const auto& point : source) {
        if (destination.empty() || destination.back() != point) { destination.push_back(point); }
    }
    while (is_closed && destination.size() > 1U && destination.back() == destination.front()) {
        destination.pop_back();
    }
    return true;
}

[[nodiscard]] auto make_offset_groups(const offset_request64& request,
                                      bool check_coordinate_range,
                                      std::vector<internal::offset_group>& groups) -> bool {
    if (request.paths.empty()) { return true; }
    Paths64 copied_paths;
    copied_paths.reserve(request.paths.size());
    const auto is_closed = internal::is_closed_path(request.end_type);
    for (const auto& path : request.paths) {
        Path64 copied_path;
        if (!copy_clean_path(path, is_closed, check_coordinate_range, copied_path)) {
            return false;
        }
        copied_paths.emplace_back(std::move(copied_path));
    }
    groups.emplace_back(std::move(copied_paths),
                        request.join_type,
                        request.end_type,
                        internal::offset_group_path_cleanliness::already_clean);
    return true;
}

[[nodiscard]] auto offset_impl(
    const offset_request64& request, bool check_coordinate_range) -> paths64_result {
    paths64_result result;
    if (std::abs(request.delta) < 0.5) {
        if (check_coordinate_range) {
            static_cast<void>(copy_paths_if_in_range(request.paths, result.closed));
        } else {
            result.closed = request.paths;
        }
        return result;
    }
    std::vector<internal::offset_group> groups;
    if (!make_offset_groups(request, check_coordinate_range, groups)) { return result; }
    auto& state = internal::acquire_reusable_offset_state();
    internal::execute_offset_algorithm(
        state,
        groups,
        request.delta,
        result.closed,
        nullptr,
        internal::offset_algorithm_options{
            .miter_limit = request.miter_limit,
            .arc_tolerance = request.arc_tolerance,
            .arc_segments_per_quadrant = request.arc_segments_per_quadrant,
            .preserve_collinear = request.options.preserve_collinear,
            .reverse_solution = request.options.reverse_solution,
            .check_input_coordinate_range = false,
            .coordinate_rounding = request.coordinate_rounding,
        },
        nullptr);
    return result;
}

}  // namespace

auto offset(const offset_request64& request) -> paths64_result {
    return offset_impl(request, false);
}

auto offset_checked(const offset_request64& request) -> expected_paths64_result {
    if (!paths_in_range(request.paths)) {
        return make_clipper_error<paths64_result>(clipper_error_code::coordinate_range);
    }
    return offset_impl(request, true);
}

auto offset_into(const offset_request64& request, paths64_result& result) -> void {
    result = offset(request);
}

}  // namespace clipper2next
