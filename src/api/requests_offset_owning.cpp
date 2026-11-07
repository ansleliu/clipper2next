#include "clipper2next/offset/operations.h"
#include "api/private/checked_execution.h"
#include "offset/private/offset_request_validation.h"

#include "clip/private/clip_request_validation.h"
#include "offset/private/offset_algorithm.h"
#include "offset/private/offset_group.h"
#include "offset/private/offset_thread_state.h"

#include "support/private/paths_rebuild.h"
#include <cmath>
#include <optional>
#include <utility>
#include <vector>

namespace clipper2next {
namespace {

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

auto make_offset_group(const offset_request64& request,
                       std::optional<internal::offset_group>& group) -> void {
    if (request.paths.empty()) { return; }
    Paths64 copied_paths;
    copied_paths.reserve(request.paths.size());
    const auto is_closed = internal::is_closed_path(request.end_type);
    for (const auto& path : request.paths) {
        Path64 copied_path;
        copy_clean_path(path, is_closed, copied_path);
        copied_paths.emplace_back(std::move(copied_path));
    }
    group.emplace(std::move(copied_paths),
                  request.join_type,
                  request.end_type,
                  internal::offset_group_path_cleanliness::already_clean);
}

auto offset_impl(const offset_request64& request, paths64_result& result) -> void {
    if (std::abs(request.delta) < 0.5) {
        // The request owns its input. Self assignment is already the zero-delta result.
        if (&request.paths != &result.closed) {
            internal::materialize_paths(request.paths, result.closed);
        }
        result.open.clear();
        return;
    }
    std::optional<internal::offset_group> group;
    make_offset_group(request, group);
    const auto groups = group
        ? std::span<const internal::offset_group>{&*group, 1U}
        : std::span<const internal::offset_group>{};
    auto lease = internal::offset_state_lease{};
    auto& state = lease.state();
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
            .coordinate_rounding = request.coordinate_rounding,
            .intersection_policy = request.options.intersection_policy,
        },
        nullptr);
    result.open.clear();
}

}  // namespace

auto offset(const offset_request64& request) -> paths64_result {
    if (const auto error = internal::validate_offset_request(request);
        error != clipper_error_code::ok) { raise_clipper_error(error); }
    paths64_result result;
    offset_impl(request, result);
    return result;
}

auto offset_checked(const offset_request64& request) -> expected_paths64_result {
    return internal::checked_call<paths64_result>([&]() -> clipper_result<paths64_result> {
        const auto error = internal::validate_offset_request(request);
        if (error != clipper_error_code::ok) { return make_clipper_error<paths64_result>(error); }
        if (!internal::clip_paths_in_range(request.paths)) {
            return make_clipper_error<paths64_result>(clipper_error_code::coordinate_range);
        }
        paths64_result result;
        offset_impl(request, result);
        return result;
    });
}

auto offset_into(const offset_request64& request, paths64_result& result) -> void {
    if (const auto error = internal::validate_offset_request(request);
        error != clipper_error_code::ok) { raise_clipper_error(error); }
    offset_impl(request, result);
}

}  // namespace clipper2next
