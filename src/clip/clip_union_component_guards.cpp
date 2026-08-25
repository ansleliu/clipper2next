#include "clip/private/boolean_union_service.h"

#include "clipper2next/geometry.h"
#include "clipper2next/geometry/core.h"
#include "geometry/private/path_simplicity.h"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace clipper2next::internal {
namespace {

constexpr std::size_t direct_singleton_component_scan_limit = 512U;
constexpr std::size_t direct_singleton_component_trim_limit = 64U;

[[nodiscard]] auto path_matches_fill_rule(const Path64& path,
                                          const clip_union_options& options) -> bool {
    const auto positive = is_positive(path);
    return (options.fill_rule == FillRule::Positive && positive) ||
           (options.fill_rule == FillRule::Negative && !positive);
}

[[nodiscard]] auto path_has_collinear_vertex(const Path64& path) noexcept -> bool {
    if (path.size() < 3U) { return true; }

    for (std::size_t index = 0; index < path.size(); ++index) {
        const auto& previous = path[(index + path.size() - 1U) % path.size()];
        const auto& current = path[index];
        const auto& next = path[(index + 1U) % path.size()];
        if (is_collinear(previous, current, next)) { return true; }
    }
    return false;
}

[[nodiscard]] auto trim_collinear_closed_component(const Path64& path) -> Path64 {
    Path64 result;
    auto source = path.cbegin();
    auto stop = path.cend() - 1;

    while (source != stop && is_collinear(*stop, *source, *(source + 1))) { ++source; }
    while (source != stop && is_collinear(*(stop - 1), *stop, *source)) { --stop; }
    if (source == stop) { return result; }

    auto previous = source++;
    result.reserve(path.size());
    result.emplace_back(*previous);
    for (; source != stop; ++source) {
        if (!is_collinear(*previous, *source, *(source + 1))) {
            previous = source;
            result.emplace_back(*previous);
        }
    }

    if (!is_collinear(*previous, *stop, result[0])) {
        result.emplace_back(*stop);
    } else {
        while (result.size() > 2U &&
               is_collinear(result[result.size() - 1U], result[result.size() - 2U], result[0])) {
            result.pop_back();
        }
        if (result.size() < 3U) { result.clear(); }
    }
    return result;
}

}  // namespace

auto try_return_singleton_component_direct(Path64& path, const clip_union_options& options)
    -> bool {
    if (!path_matches_fill_rule(path, options) || options.options.preserve_collinear ||
        path.size() < 3U) {
        return false;
    }

    if (path_has_collinear_vertex(path)) {
        if (path.size() > direct_singleton_component_trim_limit) { return false; }
        auto trimmed = trim_collinear_closed_component(path);
        if (trimmed.size() < 3U || !path_matches_fill_rule(trimmed, options) ||
            !path_simplicity::path_is_provably_simple(trimmed,
                                                       direct_singleton_component_scan_limit)) {
            return false;
        }
        if (options.options.reverse_solution) { std::reverse(trimmed.begin(), trimmed.end()); }
        path = std::move(trimmed);
        return true;
    }

    if (!path_simplicity::path_is_provably_simple(path, direct_singleton_component_scan_limit)) {
        return false;
    }
    if (options.options.reverse_solution) { std::reverse(path.begin(), path.end()); }
    return true;
}

auto try_append_singleton_component_direct(Path64& path,
                                           const clip_union_options& options,
                                           paths64_result& result) -> bool {
    if (!try_return_singleton_component_direct(path, options)) { return false; }
    result.closed.emplace_back(std::move(path));
    return true;
}

}  // namespace clipper2next::internal
