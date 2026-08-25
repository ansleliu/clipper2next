#pragma once

#include "clipper2next/geotypes/path.hpp"

#include <cstddef>
#include <compare>
#include <cstdint>
#include <limits>
#include <iterator>
#include <span>
#include <stdexcept>
#include <vector>

namespace clipper2next {

template <typename Coordinate>
class basic_path_set final {
public:
    using point_type = geotypes::Point2<Coordinate>;
    using path_view_type = geotypes::PathView<Coordinate>;
    using view_type = geotypes::PathSetView<Coordinate>;

    class const_iterator final {
    public:
        using value_type = path_view_type;
        using difference_type = std::ptrdiff_t;
        using iterator_concept = std::random_access_iterator_tag;
        using iterator_category = std::random_access_iterator_tag;

        const_iterator() = default;

        [[nodiscard]] auto operator*() const noexcept -> value_type {
            return (*owner_)[index_];
        }
        auto operator++() noexcept -> const_iterator& { ++index_; return *this; }
        auto operator++(int) noexcept -> const_iterator {
            auto previous = *this;
            ++*this;
            return previous;
        }
        auto operator--() noexcept -> const_iterator& { --index_; return *this; }
        auto operator--(int) noexcept -> const_iterator {
            auto previous = *this;
            --*this;
            return previous;
        }
        auto operator+=(difference_type offset) noexcept -> const_iterator& {
            index_ = static_cast<std::size_t>(
                static_cast<difference_type>(index_) + offset);
            return *this;
        }
        auto operator-=(difference_type offset) noexcept -> const_iterator& {
            return *this += -offset;
        }
        friend auto operator+(const_iterator value, difference_type offset) noexcept
            -> const_iterator { auto result = value; return result += offset; }
        friend auto operator+(difference_type offset, const_iterator value) noexcept
            -> const_iterator { return value + offset; }
        friend auto operator-(const_iterator value, difference_type offset) noexcept
            -> const_iterator { auto result = value; return result -= offset; }
        friend auto operator-(const const_iterator& first,
                              const const_iterator& second) noexcept
            -> difference_type {
            return static_cast<difference_type>(first.index_) -
                   static_cast<difference_type>(second.index_);
        }
        friend auto operator==(const const_iterator&, const const_iterator&) noexcept
            -> bool = default;
        friend auto operator<=>(const const_iterator&, const const_iterator&) noexcept = default;
        [[nodiscard]] auto operator[](difference_type offset) const noexcept -> value_type {
            return *(*this + offset);
        }

    private:
        friend class basic_path_set;
        const_iterator(const basic_path_set* owner, std::size_t index) noexcept
            : owner_{owner}, index_{index} {}
        const basic_path_set* owner_{};
        std::size_t index_{};
    };

    [[nodiscard]] auto empty() const noexcept -> bool { return paths_.empty(); }
    [[nodiscard]] auto size() const noexcept -> std::size_t { return paths_.size(); }
    [[nodiscard]] auto point_count() const noexcept -> std::size_t {
        return points_.size();
    }

    [[nodiscard]] auto points() const noexcept -> std::span<const point_type> {
        return points_;
    }

    [[nodiscard]] auto descriptors() const noexcept
        -> std::span<const geotypes::PathDescriptor> {
        return paths_;
    }

    [[nodiscard]] auto view() const noexcept -> view_type {
        return {points_, paths_};
    }

    [[nodiscard]] auto operator[](std::size_t index) const noexcept
        -> path_view_type {
        return view()[index];
    }

    [[nodiscard]] auto mutable_path(std::size_t index) noexcept
        -> std::span<point_type> {
        const auto& path = paths_[index];
        return std::span{points_}.subspan(path.pointOffset, path.pointCount);
    }

    [[nodiscard]] auto front() const noexcept -> path_view_type { return (*this)[0U]; }
    [[nodiscard]] auto begin() const noexcept -> const_iterator { return {this, 0U}; }
    [[nodiscard]] auto end() const noexcept -> const_iterator { return {this, size()}; }

    auto clear() noexcept -> void {
        points_.clear();
        paths_.clear();
    }

    auto reserve(std::size_t pathCount, std::size_t pointCount) -> void {
        requireRepresentable(pathCount, "path count exceeds GeoTypes descriptor range");
        requireRepresentable(pointCount, "point count exceeds GeoTypes descriptor range");
        paths_.reserve(pathCount);
        points_.reserve(pointCount);
    }

    auto append(std::span<const point_type> path,
                geotypes::PathClosure closure) -> void {
        if (paths_.size() >= maximumDescriptorValue) {
            throw std::length_error{"path count exceeds GeoTypes descriptor range"};
        }
        requireRepresentable(points_.size(),
                             "point offset exceeds GeoTypes descriptor range");
        if (path.size() > maximumDescriptorValue - points_.size()) {
            throw std::length_error{"point pool exceeds GeoTypes descriptor range"};
        }
        const auto offset = static_cast<std::uint32_t>(points_.size());
        points_.insert(points_.end(), path.begin(), path.end());
        try {
            paths_.push_back({offset,
                              static_cast<std::uint32_t>(path.size()),
                              closure,
                              {}});
        } catch (...) {
            points_.resize(offset);
            throw;
        }
    }

private:
    template <typename>
    friend class basic_path_set_builder;
    static constexpr auto maximumDescriptorValue =
        static_cast<std::size_t>((std::numeric_limits<std::uint32_t>::max)());

    static auto requireRepresentable(std::size_t value, const char* message)
        -> void {
        if (value > maximumDescriptorValue) { throw std::length_error{message}; }
    }

    std::vector<point_type> points_{};
    std::vector<geotypes::PathDescriptor> paths_{};
};

using path_set64 = basic_path_set<std::int64_t>;
using path_setd = basic_path_set<double>;

}  // namespace clipper2next
