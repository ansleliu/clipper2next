#pragma once

#include "clipper2next/clip/topology.h"

namespace clipper2next {

namespace internal {
auto release_borrowed_topology_thread_state() noexcept -> void;
}

struct borrowed_paths64_access final {
    [[nodiscard]] static auto is_bound(const borrowed_paths64& paths) noexcept -> bool {
        return paths.flat_kind_ != borrowed_paths64::flat_descriptor_kind::none ||
               (paths.source_ && paths.path_count_ && paths.measure_path_ && paths.copy_path_);
    }

    [[nodiscard]] static auto path_count(const borrowed_paths64& paths,
                                         std::size_t& result) noexcept -> clipper_error_code {
        if (paths.flat_kind_ != borrowed_paths64::flat_descriptor_kind::none) {
            result = paths.flat_descriptor_count_;
            return clipper_error_code::ok;
        }
        if (!is_bound(paths)) {
            result = 0U;
            return clipper_error_code::ok;
        }
        return paths.path_count_(paths.source_, result);
    }

    [[nodiscard]] static auto measure_path(const borrowed_paths64& paths,
                                           std::size_t path_index,
                                        path_source_contract::borrowed_path_measurement64& result) noexcept
        -> clipper_error_code {
        if (paths.flat_kind_ != borrowed_paths64::flat_descriptor_kind::none) {
            const auto source = flat_path(paths, path_index);
            if (!source) { return source.error(); }
            result = {};
            result.source_point_count = source->size();
            geotypes::Point2i64 first{};
            geotypes::Point2i64 last{};
            auto has_point = false;
            for (const auto point : *source) {
                if (has_point && point == last) { continue; }
                if (!has_point) {
                    first = point;
                    has_point = true;
                }
                last = point;
                ++result.normalized_point_count;
            }
            if (result.normalized_point_count > 1U && last == first) {
                --result.normalized_point_count;
            }
            return clipper_error_code::ok;
        }
        if (!is_bound(paths)) { return clipper_error_code::input_changed; }
        return paths.measure_path_(paths.source_, path_index, result);
    }

    [[nodiscard]] static auto copy_path(const borrowed_paths64& paths,
                                        std::size_t path_index,
                                        Point64* destination,
                                        std::size_t destination_stride,
                                        std::size_t destination_capacity,
                                        std::size_t expected_normalized_count,
                                        std::size_t& normalized_count,
                                        std::size_t& point_write_count) noexcept
        -> clipper_error_code {
        if (paths.flat_kind_ != borrowed_paths64::flat_descriptor_kind::none) {
            const auto source = flat_path(paths, path_index);
            if (!source) { return source.error(); }
            if (source->size() != destination_capacity) {
                return clipper_error_code::input_changed;
            }
            auto* destination_bytes = reinterpret_cast<std::byte*>(destination);
            geotypes::Point2i64 first{};
            geotypes::Point2i64 last{};
            auto has_point = false;
            normalized_count = 0U;
            point_write_count = 0U;
            for (const auto point : *source) {
                if (has_point && point == last) { continue; }
                if (point_write_count >= destination_capacity) {
                    return clipper_error_code::input_changed;
                }
                if (!has_point) {
                    first = point;
                    has_point = true;
                }
                *reinterpret_cast<Point64*>(
                    destination_bytes + point_write_count * destination_stride) =
                    Point64{point.x, point.y};
                last = point;
                ++point_write_count;
            }
            normalized_count = point_write_count;
            if (normalized_count > 1U && last == first) { --normalized_count; }
            return normalized_count == expected_normalized_count
                ? clipper_error_code::ok
                : clipper_error_code::input_changed;
        }
        if (!is_bound(paths)) { return clipper_error_code::input_changed; }
        return paths.copy_path_(paths.source_,
                                path_index,
                                destination,
                                destination_stride,
                                destination_capacity,
                                expected_normalized_count,
                                normalized_count,
                                point_write_count);
    }

private:
    [[nodiscard]] static auto flat_path(const borrowed_paths64& paths,
                                        const std::size_t path_index) noexcept
        -> clipper_result<std::span<const geotypes::Point2i64>> {
        if (path_index >= paths.flat_descriptor_count_) {
            return make_clipper_error<std::span<const geotypes::Point2i64>>(
                clipper_error_code::input_changed);
        }
        auto offset = std::size_t{};
        auto count = std::size_t{};
        if (paths.flat_kind_ == borrowed_paths64::flat_descriptor_kind::path) {
            const auto& descriptor =
                static_cast<const geotypes::PathDescriptor*>(
                    paths.flat_descriptors_)[path_index];
            if (descriptor.closure == geotypes::PathClosure::Open) {
                return make_clipper_error<std::span<const geotypes::Point2i64>>(
                    clipper_error_code::non_pair_input);
            }
            offset = descriptor.pointOffset;
            count = descriptor.pointCount;
        } else if (paths.flat_kind_ ==
                   borrowed_paths64::flat_descriptor_kind::ring) {
            const auto& descriptor =
                static_cast<const geotypes::RingDescriptor*>(
                    paths.flat_descriptors_)[path_index];
            offset = descriptor.pointOffset;
            count = descriptor.pointCount;
        } else {
            return make_clipper_error<std::span<const geotypes::Point2i64>>(
                clipper_error_code::input_changed);
        }
        if (offset > paths.flat_points_.size() ||
            count > paths.flat_points_.size() - offset) {
            return make_clipper_error<std::span<const geotypes::Point2i64>>(
                clipper_error_code::input_changed);
        }
        return paths.flat_points_.subspan(offset, count);
    }
};

struct topology_writer64_access final {
    [[nodiscard]] static auto is_bound(const topology_writer64& writer) noexcept -> bool {
        return writer.writer_ && writer.begin_ && writer.acquire_ && writer.finish_ &&
               writer.cancel_;
    }

    [[nodiscard]] static auto begin(topology_writer64& writer,
                                    const topology_layout64& layout) noexcept
        -> clipper_error_code {
        return is_bound(writer) ? writer.begin_(writer.writer_, layout)
                                : clipper_error_code::sink_failure;
    }

    [[nodiscard]] static auto acquire(topology_writer64& writer,
                                      const topology_ring_layout64& ring,
                                      std::span<geotypes::Point2i64>& destination) noexcept
        -> clipper_error_code {
        if (!is_bound(writer)) {
            destination = {};
            return clipper_error_code::sink_failure;
        }
        return writer.acquire_(writer.writer_, ring, destination);
    }

    [[nodiscard]] static auto finish(topology_writer64& writer) noexcept
        -> clipper_error_code {
        return is_bound(writer) ? writer.finish_(writer.writer_)
                                : clipper_error_code::sink_failure;
    }

    static auto cancel(topology_writer64& writer) noexcept -> void {
        if (is_bound(writer)) { writer.cancel_(writer.writer_); }
    }
};

}  // namespace clipper2next
