#pragma once
#include "clipper2next/geometry/core.h"
#include "geometry/private/geometry_predicates.h"
#include <span>
namespace clipper2next::internal {
[[nodiscard]] inline auto collinear_fast(const Point64& first,
                                         const Point64& shared,
                                         const Point64& second) -> bool {
    return cross_product_sign(first, shared, second) == 0;
}
template<class Output, class Collinear>
auto trim_collinear_into(std::span<const Point64> path, bool is_open_path,
                          Output& result, Collinear collinear) -> void {
    result.clear();
    auto length = path.size();
    if (length < 3) {
        if (!is_open_path || length < 2 || path[0] == path[1]) { result.clear(); return; }
        result.assign(path.begin(), path.end()); return;
    }
    result.reserve(length);
    auto source = path.cbegin();
    auto stop = path.cend() - 1;

    if (!is_open_path) {
        while (source != stop && collinear(*stop, *source, *(source + 1))) { ++source; }
        while (source != stop && collinear(*(stop - 1), *stop, *source)) { --stop; }
        if (source == stop) { result.clear(); return; }
    }

    const auto append_run = [&](auto first, auto last) {
        if (first == last) { return; }
        const auto points = std::span<const Point64>{first, last};
        if constexpr (requires { result.append_range(points); }) {
            result.append_range(points);
        } else {
            result.insert(result.end(), first, last);
        }
    };
    auto previous = source++;
    auto run_begin = previous;
    for (; source != stop; ++source) {
        if (!collinear(*previous, *source, *(source + 1))) {
            previous = source;
        } else {
            append_run(run_begin, source);
            run_begin = source + 1;
        }
    }
    append_run(run_begin, stop);

    if (is_open_path) {
        result.emplace_back(*source);
    } else if (!collinear(*previous, *stop, result[0])) {
        result.emplace_back(*stop);
    } else {
        while (result.size() > 2 &&
               collinear(result[result.size() - 1], result[result.size() - 2], result[0])) {
            result.pop_back();
        }
        if (result.size() < 3) { result.clear(); return; }
    }
    return;
}

template<class Output>
auto trim_collinear_into(std::span<const Point64> path, bool is_open_path, Output& result) -> void {
    trim_collinear_into(path, is_open_path, result,
        [](const Point64& first, const Point64& shared, const Point64& second) {
            return collinear_fast(first, shared, second);
        });
}

}
