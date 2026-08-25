// SPDX-License-Identifier: BSL-1.0

#pragma once

#include "clipper2next/core/path_set.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>

namespace clipper2next {

template <typename Coordinate>
class basic_path_set_builder final {
public:
    using owner_type = basic_path_set<Coordinate>;
    using point_type = typename owner_type::point_type;

    explicit basic_path_set_builder(owner_type& target) noexcept : target_{&target} {}
    basic_path_set_builder(const basic_path_set_builder&) = delete;
    auto operator=(const basic_path_set_builder&) -> basic_path_set_builder& = delete;
    basic_path_set_builder(basic_path_set_builder&&) = delete;
    auto operator=(basic_path_set_builder&&) -> basic_path_set_builder& = delete;
    ~basic_path_set_builder() {
        if (active_) { cancel(); }
    }

    auto begin(std::size_t path_count, std::size_t point_count) -> void {
        cancel();
        owner_type::requireRepresentable(
            path_count, "path count exceeds GeoTypes descriptor range");
        owner_type::requireRepresentable(
            point_count, "point count exceeds GeoTypes descriptor range");
        target_->paths_.reserve(path_count);
        target_->points_.resize(point_count);
        expected_path_count_ = path_count;
        expected_point_count_ = point_count;
        active_ = true;
    }

    [[nodiscard]] auto acquire(std::size_t point_count, geotypes::PathClosure closure)
        -> std::span<point_type> {
        if (!active_ || target_->paths_.size() >= expected_path_count_ ||
            point_count > expected_point_count_ - point_cursor_) {
            throw std::logic_error{"invalid flat path write sequence"};
        }
        const auto offset = point_cursor_;
        point_cursor_ += point_count;
        target_->paths_.push_back({static_cast<std::uint32_t>(offset),
                                   static_cast<std::uint32_t>(point_count),
                                   closure,
                                   {}});
        return std::span{target_->points_}.subspan(offset, point_count);
    }

    auto finish() -> void {
        if (!active_ || target_->paths_.size() != expected_path_count_ ||
            point_cursor_ != expected_point_count_) {
            throw std::logic_error{"incomplete flat path write sequence"};
        }
        active_ = false;
    }

    auto cancel() noexcept -> void {
        if (target_ != nullptr) { target_->clear(); }
        expected_path_count_ = 0U;
        expected_point_count_ = 0U;
        point_cursor_ = 0U;
        active_ = false;
    }

private:
    owner_type* target_{};
    std::size_t expected_path_count_{};
    std::size_t expected_point_count_{};
    std::size_t point_cursor_{};
    bool active_{};
};

using path_set_builder64 = basic_path_set_builder<std::int64_t>;
using path_set_builderd = basic_path_set_builder<double>;

}  // namespace clipper2next
