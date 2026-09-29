#include "clipper2next/offset/operations.h"
#include "api/private/offset_request_validation.h"

#include "clip/private/clip_request_validation.h"
#include "offset/private/offset_algorithm.h"
#include "offset/private/offset_group.h"
#include "offset/private/offset_thread_state.h"

#include <cmath>
#include <new>
#include <stdexcept>
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

auto copy_clean_path(const Path64& source, bool is_closed, Path64& destination) -> void {
    destination.clear();
    bool needs_cleanup = false;
    const Point64* previous = nullptr;
    for (const auto& point : source) {
        if (previous != nullptr && *previous == point) { needs_cleanup = true; }
        previous = &point;
    }
    needs_cleanup =
        needs_cleanup || (is_closed && source.size() > 1U && source.back() == source.front());
    if (!needs_cleanup) {
        destination.assign(source.begin(), source.end());
        return;
    }

    destination.reserve(source.size());
    for (const auto& point : source) {
        if (destination.empty() || destination.back() != point) { destination.push_back(point); }
    }
    while (is_closed && destination.size() > 1U && destination.back() == destination.front()) {
        destination.pop_back();
    }
}

auto make_offset_groups(const offset_request64& request,
                        std::vector<internal::offset_group>& groups) -> void {
    if (request.paths.empty()) { return; }
    Paths64 copied_paths;
    copied_paths.reserve(request.paths.size());
    const auto is_closed = internal::is_closed_path(request.end_type);
    for (const auto& path : request.paths) {
        Path64 copied_path;
        copy_clean_path(path, is_closed, copied_path);
        copied_paths.emplace_back(std::move(copied_path));
    }
    groups.emplace_back(std::move(copied_paths),
                        request.join_type,
                        request.end_type,
                        internal::offset_group_path_cleanliness::already_clean);
}

[[nodiscard]] auto offset_impl(const offset_request64& request)
    -> paths64_result {
    paths64_result result;
    if (std::abs(request.delta) < 0.5) {
        result.closed = request.paths;
        return result;
    }
    std::vector<internal::offset_group> groups;
    make_offset_groups(request, groups);
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
            .intersection_policy = request.options.intersection_policy,
        },
        nullptr);
    return result;
}

}  // namespace

auto offset(const offset_request64& request) -> paths64_result {
    return offset_impl(request);
}

auto offset_checked(const offset_request64& request) -> expected_paths64_result {
    try {
        const auto error = internal::validate_offset_request(request);
        if (error != clipper_error_code::ok) { return make_clipper_error<paths64_result>(error); }
        if (!paths_in_range(request.paths)) {
            return make_clipper_error<paths64_result>(clipper_error_code::coordinate_range);
        }
        return offset_impl(request);
    } catch (const std::bad_alloc&) {
        return make_clipper_error<paths64_result>(clipper_error_code::allocation_failure);
    } catch (const std::length_error&) {
        return make_clipper_error<paths64_result>(clipper_error_code::resource_limit);
    } catch (const clipper_error& error) {
        return make_clipper_error<paths64_result>(error.code());
    } catch (...) {
        return make_clipper_error<paths64_result>(clipper_error_code::internal_error);
    }
}

auto offset_into(const offset_request64& request, paths64_result& result) -> void {
    result = offset(request);
}

}  // namespace clipper2next
