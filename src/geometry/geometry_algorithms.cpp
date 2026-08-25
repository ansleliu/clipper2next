#include "clipper2next/geometry/algorithms.h"
#include "geometry/private/geometry_predicates.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace clipper2next {
namespace {

[[nodiscard]] inline auto collinear_fast(const Point64& first,
                                         const Point64& shared,
                                         const Point64& second) -> bool {
    return internal::cross_product_sign(first, shared, second) == 0;
}

template <typename Flags>
auto next_unflagged(std::size_t current, std::size_t high, const Flags& flags) -> std::size_t {
    ++current;
    while (current <= high && flags[current]) { ++current; }
    if (current <= high) { return current; }
    current = 0;
    while (flags[current]) { ++current; }
    return current;
}
template <typename Flags>
auto prior_unflagged(std::size_t current, std::size_t high, const Flags& flags) -> std::size_t {
    if (current == 0) {
        current = high;
    } else {
        --current;
    }
    while (current > 0 && flags[current]) { --current; }
    if (!flags[current]) { return current; }
    current = high;
    while (flags[current]) { --current; }
    return current;
}
auto rdp(const Path64& path,
         std::size_t begin,
         std::size_t end,
         double epsilon_squared,
         std::vector<bool>& flags) -> void {
    struct range final {
        std::size_t begin;
        std::size_t end;
    };

    std::vector<range> pending{{begin, end}};
    while (!pending.empty()) {
        auto current = pending.back();
        pending.pop_back();

        std::size_t index = 0;
        double max_distance = 0.0;
        while (current.end > current.begin &&
               path[current.begin] == path[current.end]) {
            flags[current.end--] = false;
        }
        for (auto point_index = current.begin + 1; point_index < current.end;
             ++point_index) {
            const auto distance = perpendicular_distance_from_line_squared(
                path[point_index], path[current.begin], path[current.end]);
            if (distance <= max_distance) { continue; }
            max_distance = distance;
            index = point_index;
        }
        if (max_distance <= epsilon_squared) { continue; }
        flags[index] = true;
        // Push right first so the depth-first visitation order remains identical
        // to the legacy recursive implementation while call-stack usage is bounded.
        if (index < current.end - 1) { pending.push_back({index, current.end}); }
        if (index > current.begin + 1) { pending.push_back({current.begin, index}); }
    }
}
template <typename Flags, typename Distances>
auto simplify_path_with_storage(const Path64& path,
                                double epsilon,
                                bool is_closed_path,
                                Flags& flags,
                                Distances& distances_squared) -> Path64 {
    const auto length = path.size();
    const auto high = length - 1;
    const auto epsilon_squared = square(epsilon);
    std::size_t prior = high;
    std::size_t current = 0;
    std::size_t start = 0;
    std::size_t next = 0;
    std::size_t prior2 = 0;
    if (is_closed_path) {
        distances_squared[0] =
            perpendicular_distance_from_line_squared(path[0], path[high], path[1]);
        distances_squared[high] =
            perpendicular_distance_from_line_squared(path[high], path[0], path[high - 1]);
    } else {
        distances_squared[0] = MAX_DBL;
        distances_squared[high] = MAX_DBL;
    }
    for (std::size_t index = 1; index < high; ++index) {
        distances_squared[index] =
            perpendicular_distance_from_line_squared(path[index], path[index - 1], path[index + 1]);
    }
    for (;;) {
        if (distances_squared[current] > epsilon_squared) {
            start = current;
            do {
                current = next_unflagged(current, high, flags);
            } while (current != start && distances_squared[current] > epsilon_squared);
            if (current == start) { break; }
        }
        prior = prior_unflagged(current, high, flags);
        next = next_unflagged(current, high, flags);
        if (next == prior) { break; }

        if (distances_squared[next] < distances_squared[current]) {
            prior2 = prior;
            prior = current;
            current = next;
            next = next_unflagged(next, high, flags);
        } else {
            prior2 = prior_unflagged(prior, high, flags);
        }
        flags[current] = true;
        current = next;
        next = next_unflagged(next, high, flags);
        if (is_closed_path || (current != high && current != 0)) {
            distances_squared[current] =
                perpendicular_distance_from_line_squared(path[current], path[prior], path[next]);
        }
        if (is_closed_path || (prior != 0 && prior != high)) {
            distances_squared[prior] =
                perpendicular_distance_from_line_squared(path[prior], path[prior2], path[current]);
        }
    }
    Path64 result;
    result.reserve(length);
    for (std::size_t index = 0; index < length; ++index) {
        if (!flags[index]) { result.emplace_back(path[index]); }
    }
    return result;
}
}  // namespace
auto trim_collinear(const Path64& path, bool is_open_path) -> Path64 {
    auto length = path.size();
    if (length < 3) {
        if (!is_open_path || length < 2 || path[0] == path[1]) { return {}; }
        return path;
    }
    Path64 result;
    result.reserve(length);
    auto source = path.cbegin();
    auto stop = path.cend() - 1;

    if (!is_open_path) {
        while (source != stop && collinear_fast(*stop, *source, *(source + 1))) { ++source; }
        while (source != stop && collinear_fast(*(stop - 1), *stop, *source)) { --stop; }
        if (source == stop) { return {}; }
    }

    auto previous = source++;
    result.emplace_back(*previous);
    for (; source != stop; ++source) {
        if (!collinear_fast(*previous, *source, *(source + 1))) {
            previous = source;
            result.emplace_back(*previous);
        }
    }

    if (is_open_path) {
        result.emplace_back(*source);
    } else if (!collinear_fast(*previous, *stop, result[0])) {
        result.emplace_back(*stop);
    } else {
        while (result.size() > 2 &&
               collinear_fast(result[result.size() - 1], result[result.size() - 2], result[0])) {
            result.pop_back();
        }
        if (result.size() < 3) { return {}; }
    }
    return result;
}

auto simplify_path(const Path64& path, double epsilon, bool is_closed_path) -> Path64 {
    const auto length = path.size();
    if (length < 4) { return path; }
    if (length <= 64U) {
        std::array<unsigned char, 64U> flags{};
        std::array<double, 64U> distances_squared{};
        return simplify_path_with_storage(path, epsilon, is_closed_path, flags, distances_squared);
    }
    std::vector<unsigned char> flags(length);
    std::vector<double> distances_squared(length);
    return simplify_path_with_storage(path, epsilon, is_closed_path, flags, distances_squared);
}

auto simplify_paths(const Paths64& paths, double epsilon, bool is_closed_path) -> Paths64 {
    Paths64 result;
    result.reserve(paths.size());
    for (const auto& path : paths) {
        result.emplace_back(simplify_path(path, epsilon, is_closed_path));
    }
    return result;
}

auto ramer_douglas_peucker(const Path64& path, double epsilon) -> Path64 {
    const auto length = path.size();
    if (length < 5) { return path; }
    if (std::isnan(epsilon)) { return path; }
    std::vector<bool> flags(length);
    flags[0] = true;
    flags[length - 1] = true;
    rdp(path, 0, length - 1, square(epsilon), flags);
    Path64 result;
    result.reserve(length);
    for (std::size_t index = 0; index < length; ++index) {
        if (flags[index]) { result.emplace_back(path[index]); }
    }
    return result;
}

auto ramer_douglas_peucker(const Paths64& paths, double epsilon) -> Paths64 {
    Paths64 result;
    result.reserve(paths.size());
    for (const auto& path : paths) { result.emplace_back(ramer_douglas_peucker(path, epsilon)); }
    return result;
}

}  // namespace clipper2next
