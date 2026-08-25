#pragma once

#include "../support/path_equivalence.h"

#include "clipper2/clipper.h"

namespace clipper2next::tests::oracle {

namespace legacy = Clipper2Lib;

[[nodiscard]] inline auto to_next_path(const legacy::Path64& path) -> next::Path64 {
    next::Path64 result;
    result.reserve(path.size());
    for (const auto& point : path) { result.push_back(next::Point64{point.x, point.y}); }
    return result;
}

[[nodiscard]] inline auto to_next_paths(const legacy::Paths64& paths) -> next::Paths64 {
    next::Paths64 result;
    result.reserve(paths.size());
    for (const auto& path : paths) { result.push_back(to_next_path(path)); }
    return result;
}

[[nodiscard]] inline auto to_legacy_path(const next::Path64& path) -> legacy::Path64 {
    legacy::Path64 result;
    result.reserve(path.size());
    for (const auto& point : path) { result.push_back(legacy::Point64{point.x, point.y}); }
    return result;
}

[[nodiscard]] inline auto to_legacy_paths(const next::Paths64& paths) -> legacy::Paths64 {
    legacy::Paths64 result;
    result.reserve(paths.size());
    for (const auto& path : paths) { result.push_back(to_legacy_path(path)); }
    return result;
}

inline auto assert_paths_semantically_equal(const legacy::Paths64& expected,
                                            const next::Paths64& actual) -> void {
    assert_paths_semantically_equal(to_next_paths(expected), actual);
}

inline auto assert_open_paths_exactly_equal(const legacy::Paths64& expected,
                                            const next::Paths64& actual) -> void {
    assert_open_paths_exactly_equal(to_next_paths(expected), actual);
}

}  // namespace clipper2next::tests::oracle
