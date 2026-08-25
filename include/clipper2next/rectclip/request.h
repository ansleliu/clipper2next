// SPDX-License-Identifier: BSL-1.0

#pragma once

#include "clipper2next/api/export.h"
#include "clipper2next/api/result.h"

#include <memory>
#include <utility>
#include <vector>

namespace clipper2next {

struct rect_clip_request64 final {
    Rect64 rect{};
    Paths64 paths{};
};

struct rect_clip_lines_request64 final {
    Rect64 rect{};
    Paths64 lines{};
};

struct prepared_rect_clip_request64 final {
    prepared_rect_clip_request64() = default;

    [[nodiscard]] auto request() const noexcept -> const rect_clip_request64& {
        return request_;
    }

private:
    friend CLIPPER2NEXT_API auto prepare_rect_clip_request(
        rect_clip_request64 request)
        -> prepared_rect_clip_request64;
    friend CLIPPER2NEXT_API auto rect_clip(
        const prepared_rect_clip_request64& request) -> rect_clip_result64;

    prepared_rect_clip_request64(rect_clip_request64 request, std::vector<Rect64> path_bounds)
        : request_(std::move(request)), path_bounds_(std::move(path_bounds)) {}

    rect_clip_request64 request_{};
    std::vector<Rect64> path_bounds_{};
};

struct immutable_rect_clip_paths64 final {
    immutable_rect_clip_paths64() = default;

private:
    struct runtime_data;

    friend CLIPPER2NEXT_API auto prepare_immutable_rect_clip_paths(Paths64 paths)
        -> immutable_rect_clip_paths64;
    friend CLIPPER2NEXT_API auto rect_clip(
        const Rect64& rect,
        const immutable_rect_clip_paths64& immutable_paths)
        -> rect_clip_result64;

    explicit immutable_rect_clip_paths64(
        std::shared_ptr<const runtime_data> runtime_data)
        : runtime_data_(std::move(runtime_data)) {}

    std::shared_ptr<const runtime_data> runtime_data_{};
};

[[nodiscard]] CLIPPER2NEXT_API auto rect_clip(const rect_clip_request64& request)
    -> rect_clip_result64;
[[nodiscard]] CLIPPER2NEXT_API auto rect_clip_checked(
    const rect_clip_request64& request)
    -> expected_rect_clip_result64;
CLIPPER2NEXT_API auto rect_clip_into(
    const rect_clip_request64& request, rect_clip_result64& result) -> void;
[[nodiscard]] CLIPPER2NEXT_API auto prepare_rect_clip_request(
    rect_clip_request64 request)
    -> prepared_rect_clip_request64;
[[nodiscard]] CLIPPER2NEXT_API auto rect_clip(
    const prepared_rect_clip_request64& request) -> rect_clip_result64;
CLIPPER2NEXT_API auto rect_clip_into(
    const prepared_rect_clip_request64& request, rect_clip_result64& result)
    -> void;
[[nodiscard]] CLIPPER2NEXT_API auto prepare_immutable_rect_clip_paths(Paths64 paths)
    -> immutable_rect_clip_paths64;
[[nodiscard]] CLIPPER2NEXT_API auto rect_clip(
    const Rect64& rect, const immutable_rect_clip_paths64& immutable_paths)
    -> rect_clip_result64;
CLIPPER2NEXT_API auto rect_clip_into(
    const Rect64& rect,
    const immutable_rect_clip_paths64& immutable_paths,
    rect_clip_result64& result) -> void;

[[nodiscard]] CLIPPER2NEXT_API auto rect_clip_lines(
    const rect_clip_lines_request64& request) -> rect_clip_result64;
[[nodiscard]] CLIPPER2NEXT_API auto rect_clip_lines_checked(
    const rect_clip_lines_request64& request)
    -> expected_rect_clip_result64;
CLIPPER2NEXT_API auto rect_clip_lines_into(
    const rect_clip_lines_request64& request, rect_clip_result64& result)
    -> void;

}  // namespace clipper2next
