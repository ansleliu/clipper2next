#pragma once

#include "clipper2next/clip/types.h"

#include <algorithm>
#include <cstddef>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace clipper2next::tests::oracle {

namespace next = clipper2next;

[[nodiscard]] inline auto same_point(const next::Point64& lhs, const next::Point64& rhs) -> bool {
    return lhs.x == rhs.x && lhs.y == rhs.y;
}

[[nodiscard]] inline auto point_less(const next::Point64& lhs, const next::Point64& rhs) -> bool {
    return lhs.x != rhs.x ? lhs.x < rhs.x : lhs.y < rhs.y;
}

[[nodiscard]] inline auto path_less(const next::Path64& lhs, const next::Path64& rhs) -> bool {
    return std::lexicographical_compare(lhs.begin(), lhs.end(), rhs.begin(), rhs.end(), point_less);
}

[[nodiscard]] inline auto trim_duplicate_closure(next::Path64 path) -> next::Path64 {
    if (path.size() > 1U && same_point(path.front(), path.back())) { path.pop_back(); }
    return path;
}

[[nodiscard]] inline auto rotate_path(const next::Path64& path, std::size_t offset)
    -> next::Path64 {
    next::Path64 result;
    result.reserve(path.size());
    for (std::size_t index = 0; index < path.size(); ++index) {
        result.push_back(path[(offset + index) % path.size()]);
    }
    return result;
}

[[nodiscard]] inline auto canonical_closed_path(next::Path64 path) -> next::Path64 {
    path = trim_duplicate_closure(std::move(path));
    if (path.size() < 2U) { return path; }
    auto best = rotate_path(path, 0U);
    for (std::size_t offset = 1U; offset < path.size(); ++offset) {
        auto candidate = rotate_path(path, offset);
        if (path_less(candidate, best)) { best = std::move(candidate); }
    }
    return best;
}

[[nodiscard]] inline auto canonical_closed_paths(next::Paths64 paths) -> next::Paths64 {
    for (auto& path : paths) { path = canonical_closed_path(std::move(path)); }
    std::sort(paths.begin(), paths.end(), path_less);
    return paths;
}

[[nodiscard]] inline auto canonical_open_paths(next::Paths64 paths) -> next::Paths64 {
    std::sort(paths.begin(), paths.end(), path_less);
    return paths;
}

[[nodiscard]] inline auto describe_path(const next::Path64& path) -> std::string {
    constexpr auto maximum_points = std::size_t{16U};
    std::ostringstream output;
    output << '[';
    const auto count = (std::min)(path.size(), maximum_points);
    for (std::size_t index = 0; index < count; ++index) {
        if (index != 0U) { output << ' '; }
        output << '(' << path[index].x << ',' << path[index].y << ')';
    }
    if (path.size() > count) { output << " ..."; }
    output << "] points=" << path.size();
    return output.str();
}

[[nodiscard]] inline auto first_point_difference(const next::Path64& expected,
                                                  const next::Path64& actual) -> std::size_t {
    const auto count = (std::min)(expected.size(), actual.size());
    for (std::size_t index = 0; index < count; ++index) {
        if (!same_point(expected[index], actual[index])) { return index; }
    }
    return count;
}

[[nodiscard]] inline auto describe_path_window(const next::Path64& path, std::size_t center)
    -> std::string {
    constexpr auto radius = std::size_t{3U};
    const auto begin = center > radius ? center - radius : 0U;
    const auto end = (std::min)(path.size(), center + radius + 1U);
    std::ostringstream output;
    output << "index=" << center << " [";
    for (auto index = begin; index < end; ++index) {
        if (index != begin) { output << ' '; }
        output << index << ":(" << path[index].x << ',' << path[index].y << ')';
    }
    output << "] points=" << path.size();
    return output.str();
}

[[nodiscard]] inline auto first_unmatched_path(const next::Paths64& source,
                                                const next::Paths64& other)
    -> const next::Path64* {
    for (const auto& path : source) {
        if (std::find(other.begin(), other.end(), path) == other.end()) { return &path; }
    }
    return nullptr;
}

[[nodiscard]] inline auto describe_unmatched_paths(const next::Paths64& source,
                                                    const next::Paths64& other) -> std::string {
    constexpr auto maximum_paths = std::size_t{4U};
    std::ostringstream output;
    std::size_t emitted{};
    for (const auto& path : source) {
        if (std::find(other.begin(), other.end(), path) != other.end()) { continue; }
        if (emitted != 0U) { output << " | "; }
        output << describe_path(path);
        if (++emitted == maximum_paths) { break; }
    }
    return output.str();
}

inline auto assert_paths_semantically_equal(const next::Paths64& expected,
                                            const next::Paths64& actual) -> void {
    const auto canonical_expected = canonical_closed_paths(expected);
    const auto canonical_actual = canonical_closed_paths(actual);
    if (canonical_expected.size() != canonical_actual.size()) {
        auto message = std::string{"path count mismatch: expected "} +
            std::to_string(canonical_expected.size()) + ", actual " +
            std::to_string(canonical_actual.size());
        if (first_unmatched_path(canonical_expected, canonical_actual) != nullptr) {
            message += "; missing " + describe_unmatched_paths(canonical_expected, canonical_actual);
        }
        if (const auto* unexpected = first_unmatched_path(canonical_actual, canonical_expected)) {
            message += "; unexpected " +
                describe_unmatched_paths(canonical_actual, canonical_expected);
            if (const auto* missing = first_unmatched_path(canonical_expected, canonical_actual)) {
                const auto difference = first_point_difference(*missing, *unexpected);
                message += "; unmatched divergence expected " +
                    describe_path_window(*missing, difference) + "; actual " +
                    describe_path_window(*unexpected, difference);
            }
        }
        throw std::runtime_error{message};
    }

    for (std::size_t path_index = 0; path_index < canonical_expected.size(); ++path_index) {
        const auto& expected_path = canonical_expected[path_index];
        const auto& actual_path = canonical_actual[path_index];
        if (expected_path.size() != actual_path.size()) {
            const auto difference = first_point_difference(expected_path, actual_path);
            throw std::runtime_error{"point count mismatch in path " +
                                     std::to_string(path_index) + ": expected " +
                                     std::to_string(expected_path.size()) + ", actual " +
                                     std::to_string(actual_path.size()) + "; expected " +
                                     describe_path_window(expected_path, difference) +
                                     "; actual " +
                                     describe_path_window(actual_path, difference)};
        }
        for (std::size_t point_index = 0; point_index < expected_path.size(); ++point_index) {
            if (!same_point(expected_path[point_index], actual_path[point_index])) {
                throw std::runtime_error{"point mismatch at path " +
                                         std::to_string(path_index) + ", point " +
                                         std::to_string(point_index)};
            }
        }
    }
}

inline auto assert_open_paths_exactly_equal(const next::Paths64& expected,
                                            const next::Paths64& actual) -> void {
    const auto canonical_expected = canonical_open_paths(expected);
    const auto canonical_actual = canonical_open_paths(actual);
    if (canonical_expected.size() != canonical_actual.size()) {
        throw std::runtime_error{"open path count mismatch: expected " +
                                 std::to_string(canonical_expected.size()) + ", actual " +
                                 std::to_string(canonical_actual.size())};
    }
    for (std::size_t path_index = 0; path_index < canonical_expected.size(); ++path_index) {
        const auto& expected_path = canonical_expected[path_index];
        const auto& actual_path = canonical_actual[path_index];
        if (expected_path.size() != actual_path.size()) {
            throw std::runtime_error{"open point count mismatch in path " +
                                     std::to_string(path_index) + ": expected " +
                                     std::to_string(expected_path.size()) + ", actual " +
                                     std::to_string(actual_path.size())};
        }
        for (std::size_t point_index = 0; point_index < expected_path.size(); ++point_index) {
            if (!same_point(expected_path[point_index], actual_path[point_index])) {
                throw std::runtime_error{"open point mismatch at path " +
                                         std::to_string(path_index) + ", point " +
                                         std::to_string(point_index)};
            }
        }
    }
}

}  // namespace clipper2next::tests::oracle
