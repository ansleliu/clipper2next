#pragma once

#include "clipper2next/geometry.h"
#include "clipper2next/offset/types.h"
#include "offset/private/offset_geometry.h"

#include <limits>
#include <memory_resource>
#include <optional>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace clipper2next::internal {

enum class offset_group_path_cleanliness {
    needs_cleanup,
    already_clean,
};

struct offset_path_record final {
    std::size_t point_offset{};
    std::size_t point_count{};
};

struct flat_offset_paths64 final {
    std::pmr::vector<Point64> points;
    std::vector<offset_path_record> paths{};

    explicit flat_offset_paths64(
        std::pmr::memory_resource* resource = std::pmr::get_default_resource())
        : points(resource) {}
};

struct lowest_closed_path_info final {
    std::optional<std::size_t> path_index{};
    bool has_negative_area{false};
};

struct offset_group final {
    Paths64 owned_paths;
    std::optional<flat_offset_paths64> flat_paths{};
    std::optional<std::size_t> lowest_path_index{};
    bool is_reversed{false};
    JoinType join_type{JoinType::Bevel};
    EndType end_type{EndType::Polygon};

    offset_group(const Paths64& paths, JoinType join_type_value, EndType end_type_value)
        : owned_paths(paths),
          join_type(join_type_value),
          end_type(end_type_value) {
        prepare_paths(offset_group_path_cleanliness::needs_cleanup);
    }

    offset_group(Paths64&& paths, JoinType join_type_value, EndType end_type_value)
        : owned_paths(std::move(paths)),
          join_type(join_type_value),
          end_type(end_type_value) {
        prepare_paths(offset_group_path_cleanliness::needs_cleanup);
    }

    offset_group(Paths64&& paths,
                 JoinType join_type_value,
                 EndType end_type_value,
                 offset_group_path_cleanliness cleanliness)
        : owned_paths(std::move(paths)),
          join_type(join_type_value),
          end_type(end_type_value) {
        prepare_paths(cleanliness);
    }

    offset_group(flat_offset_paths64&& paths,
                 JoinType join_type_value,
                 EndType end_type_value)
        : flat_paths(std::move(paths)),
          join_type(join_type_value),
          end_type(end_type_value) {
        prepare_paths(offset_group_path_cleanliness::already_clean);
    }

    [[nodiscard]] auto path_count() const noexcept -> std::size_t {
        return flat_paths ? flat_paths->paths.size() : owned_paths.size();
    }

    [[nodiscard]] auto path(std::size_t index) const -> std::span<const Point64> {
        if (!flat_paths) {
            const auto& value = owned_paths.at(index);
            return {value.data(), value.size()};
        }
        const auto record = flat_paths->paths.at(index);
        return std::span<const Point64>{flat_paths->points}.subspan(
            record.point_offset, record.point_count);
    }

    auto prepare_paths(offset_group_path_cleanliness cleanliness) -> void {
        if (flat_paths && cleanliness == offset_group_path_cleanliness::needs_cleanup) {
            throw std::logic_error{"flat offset paths must be normalized before construction"};
        }
        if (!flat_paths && cleanliness == offset_group_path_cleanliness::needs_cleanup) {
            strip_duplicates(owned_paths, internal::is_closed_path(end_type));
        }

        if (end_type != EndType::Polygon) { return; }

        auto lowest_info = lowest_closed_path_info{};
        auto bottom_point = Point64{(std::numeric_limits<std::int64_t>::max)(),
                                    (std::numeric_limits<std::int64_t>::min)()};
        for (std::size_t path_index = 0; path_index < path_count(); ++path_index) {
            const auto candidate = path(path_index);
            auto path_area = MAX_DBL;
            for (const auto& point : candidate) {
                if ((point.y < bottom_point.y) ||
                    ((point.y == bottom_point.y) && (point.x >= bottom_point.x))) {
                    continue;
                }
                if (path_area == MAX_DBL) {
                    path_area = area(candidate);
                    if (path_area == 0.0) { break; }
                    lowest_info.has_negative_area = path_area < 0.0;
                }
                lowest_info.path_index = path_index;
                bottom_point = point;
            }
        }
        lowest_path_index = lowest_info.path_index;
        is_reversed = lowest_path_index.has_value() && lowest_info.has_negative_area;
    }
};

}  // namespace clipper2next::internal
