#pragma once

#include "clipper2next/geotypes/point.hpp"

#include <cstdint>
#include <span>

namespace geotypes {

enum class PathClosure : std::uint8_t {
    Open,
    ClosedImplicit,
    ClosedRepeated,
};

struct PathDescriptor final {
    std::uint32_t pointOffset{};
    std::uint32_t pointCount{};
    PathClosure closure{PathClosure::Open};
    std::uint8_t reserved[3]{};
};

template <class T>
struct PathView final {
    std::span<const Point2<T>> points{};
    PathClosure closure{PathClosure::Open};

    [[nodiscard]] constexpr auto begin() const noexcept { return points.begin(); }
    [[nodiscard]] constexpr auto end() const noexcept { return points.end(); }
    [[nodiscard]] constexpr auto empty() const noexcept -> bool { return points.empty(); }
    [[nodiscard]] constexpr auto size() const noexcept -> std::size_t { return points.size(); }
    [[nodiscard]] constexpr auto data() const noexcept { return points.data(); }
    [[nodiscard]] constexpr auto front() const noexcept -> const Point2<T>& {
        return points.front();
    }
    [[nodiscard]] constexpr auto back() const noexcept -> const Point2<T>& {
        return points.back();
    }
    [[nodiscard]] constexpr auto operator[](std::size_t index) const noexcept
        -> const Point2<T>& {
        return points[index];
    }
};

template <class T>
struct PathSetView final {
    std::span<const Point2<T>> points{};
    std::span<const PathDescriptor> paths{};

    [[nodiscard]] constexpr auto empty() const noexcept -> bool { return paths.empty(); }
    [[nodiscard]] constexpr auto size() const noexcept -> std::size_t { return paths.size(); }
    [[nodiscard]] constexpr auto operator[](std::size_t index) const noexcept
        -> PathView<T> {
        const auto& path = paths[index];
        return {points.subspan(path.pointOffset, path.pointCount), path.closure};
    }
};

using PathView64 = PathView<std::int64_t>;
using PathSetView64 = PathSetView<std::int64_t>;

}  // namespace geotypes
