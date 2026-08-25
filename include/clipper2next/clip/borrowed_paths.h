// SPDX-License-Identifier: BSL-1.0

#pragma once

#include "clipper2next/clip/path_source.h"
#include "clipper2next/geotypes/topology.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <type_traits>

namespace clipper2next {

class borrowed_paths64;

template <path_source_contract::borrowable_paths64_source Source>
[[nodiscard]] auto borrow_paths64(Source& source) noexcept -> borrowed_paths64;

[[nodiscard]] auto borrow_paths64(geotypes::PathSetView64 source) noexcept -> borrowed_paths64;
[[nodiscard]] auto borrow_paths64(geotypes::RingSetView64 source) noexcept -> borrowed_paths64;
[[nodiscard]] auto borrow_paths64(geotypes::TopologyView64 source) noexcept -> borrowed_paths64;

class borrowed_paths64 final {
public:
    borrowed_paths64() = default;

    // The source must remain alive and unchanged until execution returns.

private:
    using path_count_function = clipper_error_code (*)(const void*, std::size_t&) noexcept;
    using measure_path_function = clipper_error_code (*)(
        const void*, std::size_t, path_source_contract::borrowed_path_measurement64&) noexcept;
    using copy_path_function = clipper_error_code (*)(const void*,
                                                      std::size_t,
                                                      Point64*,
                                                      std::size_t,
                                                      std::size_t,
                                                      std::size_t,
                                                      std::size_t&,
                                                      std::size_t&) noexcept;

    template <path_source_contract::borrowable_paths64_source Source>
    friend auto borrow_paths64(Source& source) noexcept -> borrowed_paths64;
    friend auto borrow_paths64(geotypes::PathSetView64 source) noexcept -> borrowed_paths64;
    friend auto borrow_paths64(geotypes::RingSetView64 source) noexcept -> borrowed_paths64;
    friend auto borrow_paths64(geotypes::TopologyView64 source) noexcept -> borrowed_paths64;
    friend struct borrowed_paths64_access;

    const void* source_{};
    path_count_function path_count_{};
    measure_path_function measure_path_{};
    copy_path_function copy_path_{};
    enum class flat_descriptor_kind : std::uint8_t { none, path, ring };

    std::span<const geotypes::Point2i64> flat_points_{};
    const void* flat_descriptors_{};
    std::size_t flat_descriptor_count_{};
    flat_descriptor_kind flat_kind_{flat_descriptor_kind::none};
};

template <path_source_contract::borrowable_paths64_source Source>
[[nodiscard]] auto borrow_paths64(Source& source) noexcept -> borrowed_paths64 {
    using source_type = std::remove_cv_t<Source>;
    auto result = borrowed_paths64{};
    result.source_ = std::addressof(source);
    result.path_count_ = &path_source_contract::borrowed_path_count<source_type>;
    result.measure_path_ = &path_source_contract::measure_borrowed_path<source_type>;
    result.copy_path_ = &path_source_contract::copy_borrowed_path<source_type>;
    return result;
}

[[nodiscard]] inline auto borrow_paths64(geotypes::PathSetView64 source) noexcept
    -> borrowed_paths64 {
    auto result = borrowed_paths64{};
    result.flat_points_ = source.points;
    result.flat_descriptors_ = source.paths.data();
    result.flat_descriptor_count_ = source.paths.size();
    result.flat_kind_ = borrowed_paths64::flat_descriptor_kind::path;
    return result;
}

[[nodiscard]] inline auto borrow_paths64(geotypes::TopologyView64 source) noexcept
    -> borrowed_paths64 {
    return borrow_paths64(source.ringSet());
}

[[nodiscard]] inline auto borrow_paths64(geotypes::RingSetView64 source) noexcept
    -> borrowed_paths64 {
    auto result = borrowed_paths64{};
    result.flat_points_ = source.points;
    result.flat_descriptors_ = source.rings.data();
    result.flat_descriptor_count_ = source.rings.size();
    result.flat_kind_ = borrowed_paths64::flat_descriptor_kind::ring;
    return result;
}

}  // namespace clipper2next
