#pragma once

#include "clipper2next/api/export.h"
#include "clipper2next/core/path.h"

#include <algorithm>

namespace clipper2next {

template <typename T>
[[nodiscard]] inline auto translate(const Path<T>& path, T dx, T dy) -> Path<T> {
    const auto count = path.size();
    auto translated = Path<T>{};
    translated.resize(count);
    const auto* source = path.data();
    auto* target = translated.data();
    for (std::size_t index = 0; index < count; ++index) {
        target[index].x = geotypes::saturatedAdd(source[index].x, dx);
        target[index].y = geotypes::saturatedAdd(source[index].y, dy);
    }
    return translated;
}

[[nodiscard]] CLIPPER2NEXT_API auto translate(
    const Path64& path, int64_t dx, int64_t dy) -> Path64;
[[nodiscard]] CLIPPER2NEXT_API auto translate(
    const PathD& path, double dx, double dy) -> PathD;

template <typename T>
[[nodiscard]] inline auto translate(const Paths<T>& paths, T dx, T dy) -> Paths<T> {
    auto translated = Paths<T>{};
    translated.reserve(paths.size());
    for (const auto& path : paths) { translated.push_back(translate(path, dx, dy)); }
    return translated;
}

[[nodiscard]] CLIPPER2NEXT_API auto translate(
    const Paths64& paths, int64_t dx, int64_t dy) -> Paths64;
[[nodiscard]] CLIPPER2NEXT_API auto translate(
    const PathsD& paths, double dx, double dy) -> PathsD;

}  // namespace clipper2next
