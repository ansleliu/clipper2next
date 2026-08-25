// SPDX-License-Identifier: BSL-1.0

#pragma once

#include "clipper2next/api/error.h"
#include "clipper2next/geotypes/point.hpp"

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <span>

namespace clipper2next {

enum class topology_ring_role : std::uint8_t { shell, hole };

inline constexpr std::size_t topology_no_polygon_index =
    (std::numeric_limits<std::size_t>::max)();

struct topology_polygon_layout64 final {
    std::size_t parent_polygon_index{topology_no_polygon_index};
    std::size_t ring_count{};
    std::size_t point_count{};
};

struct topology_layout64 final {
    // The polygon span remains valid only for the duration of writer.begin().
    std::span<const topology_polygon_layout64> polygons{};
    std::size_t ring_count{};
    std::size_t point_count{};
    std::size_t maximum_ring_point_count{};
    std::size_t staging_workspace_bytes{};
};

struct topology_ring_layout64 final {
    std::size_t polygon_index{};
    topology_ring_role role{topology_ring_role::shell};
    std::size_t point_count{};
};

template <typename Writer>
concept topology_writer64_target = requires(Writer& writer,
                                             const topology_layout64& layout,
                                             const topology_ring_layout64& ring,
                                             std::span<geotypes::Point2i64>& destination) {
    { writer.begin(layout) } -> std::convertible_to<clipper_error_code>;
    { writer.acquire(ring, destination) } -> std::convertible_to<clipper_error_code>;
    { writer.finish() } -> std::convertible_to<clipper_error_code>;
    { writer.cancel() };
};

class topology_writer64;

template <topology_writer64_target Writer>
[[nodiscard]] auto make_topology_writer64(Writer& writer) noexcept -> topology_writer64;

class topology_writer64 final {
public:
    topology_writer64() = default;

private:
    using begin_function = clipper_error_code (*)(void*, const topology_layout64&) noexcept;
    using acquire_function = clipper_error_code (*)(
        void*, const topology_ring_layout64&, std::span<geotypes::Point2i64>&) noexcept;
    using finish_function = clipper_error_code (*)(void*) noexcept;
    using cancel_function = void (*)(void*) noexcept;

    template <topology_writer64_target Writer>
    friend auto make_topology_writer64(Writer& writer) noexcept -> topology_writer64;
    friend struct topology_writer64_access;

    void* writer_{};
    begin_function begin_{};
    acquire_function acquire_{};
    finish_function finish_{};
    cancel_function cancel_{};
};

template <topology_writer64_target Writer>
[[nodiscard]] auto make_topology_writer64(Writer& writer) noexcept -> topology_writer64 {
    auto result = topology_writer64{};
    result.writer_ = std::addressof(writer);
    result.begin_ = [](void* target, const topology_layout64& layout) noexcept {
        try {
            return static_cast<clipper_error_code>(static_cast<Writer*>(target)->begin(layout));
        } catch (const std::bad_alloc&) {
            return clipper_error_code::allocation_failure;
        } catch (...) {
            return clipper_error_code::sink_failure;
        }
    };
    result.acquire_ = [](void* target,
                         const topology_ring_layout64& ring,
                         std::span<geotypes::Point2i64>& destination) noexcept {
        try {
            destination = {};
            return static_cast<clipper_error_code>(
                static_cast<Writer*>(target)->acquire(ring, destination));
        } catch (const std::bad_alloc&) {
            destination = {};
            return clipper_error_code::allocation_failure;
        } catch (...) {
            destination = {};
            return clipper_error_code::sink_failure;
        }
    };
    result.finish_ = [](void* target) noexcept {
        try {
            return static_cast<clipper_error_code>(static_cast<Writer*>(target)->finish());
        } catch (const std::bad_alloc&) {
            return clipper_error_code::allocation_failure;
        } catch (...) {
            return clipper_error_code::sink_failure;
        }
    };
    result.cancel_ = [](void* target) noexcept {
        try {
            static_cast<Writer*>(target)->cancel();
        } catch (...) {
        }
    };
    return result;
}

}  // namespace clipper2next
