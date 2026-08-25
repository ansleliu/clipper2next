#pragma once

#include "clipper2next/core/point.h"

#include <algorithm>
#include <ostream>
#include <vector>

namespace clipper2next {

namespace path_storage {

template <typename Coordinate>
using point_sequence = std::vector<Point<Coordinate>>;

template <typename Coordinate>
using path_sequence = std::vector<point_sequence<Coordinate>>;

}  // namespace path_storage

template <typename T>
using Path = path_storage::point_sequence<T>;

template <typename T>
using Paths = path_storage::path_sequence<T>;

// The returned reference enables chaining but may be ignored; the mutation
// has already happened in place.
template <typename T, typename T2 = T>
inline auto append_point(Path<T>& path, const Point<T2>& point) -> Path<T>& {
    path.emplace_back(point);
    return path;
}

template <typename T>
inline auto append_path(Paths<T>& paths, const Path<T>& path) -> Paths<T>& {
    paths.emplace_back(path);
    return paths;
}

using Path64 = Path<int64_t>;
using PathD = Path<double>;
using Paths64 = Paths<int64_t>;
using PathsD = Paths<double>;
using path64 = Path64;
using pathd = PathD;
using paths64 = Paths64;
using pathsd = PathsD;

template <typename T>
auto write_path(std::ostream& outstream, const Path<T>& path) -> std::ostream&;

template <typename T>
auto write_paths(std::ostream& outstream, const Paths<T>& paths) -> std::ostream&;

template <typename T>
auto write_path(std::ostream& outstream, const Path<T>& path) -> std::ostream& {
    if (!path.empty()) {
        auto point = path.cbegin();
        const auto last = path.cend() - 1;
        while (point != last) {
            outstream << point->x << ',' << point->y << ", ";
            ++point;
        }
        outstream << last->x << ',' << last->y << '\n';
    }
    return outstream;
}

template <typename T>
auto write_paths(std::ostream& outstream, const Paths<T>& paths) -> std::ostream& {
    for (const auto& path : paths) { write_path(outstream, path); }
    return outstream;
}

template <typename T>
inline void erase_consecutive_duplicates(Path<T>& path) {
    const auto unique_end = std::unique(path.begin(), path.end());
    path.erase(unique_end, path.end());
}

template <typename T>
inline void strip_duplicates(Path<T>& path, bool is_closed_path) {
    erase_consecutive_duplicates(path);
    if (!is_closed_path) { return; }
    while (path.size() > 1 && path.back() == path.front()) { path.pop_back(); }
}

template <typename T>
inline void strip_duplicates(Paths<T>& paths, bool is_closed_path) {
    for (auto& path : paths) { strip_duplicates(path, is_closed_path); }
}

}  // namespace clipper2next
