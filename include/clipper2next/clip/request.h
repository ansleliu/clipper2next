#pragma once

#include "clipper2next/api/export.h"
#include "clipper2next/api/options.h"
#include "clipper2next/api/result.h"
#include "clipper2next/clip/types.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <utility>

namespace clipper2next {

struct clip_request64 final {
    ClipType clip_type{ClipType::NoClip};
    FillRule fill_rule{FillRule::EvenOdd};
    Paths64 subjects{};
    Paths64 open_subjects{};
    Paths64 clips{};
    execution_options options{.preserve_collinear = true};
};

struct clip_request_metadata64 final {
    std::optional<Rect64> single_subject_rect{};
    std::optional<Rect64> single_clip_rect{};
    std::size_t subject_path_count{};
    std::size_t open_subject_path_count{};
    std::size_t clip_path_count{};
    std::size_t subject_point_count{};
    std::size_t open_subject_point_count{};
    std::size_t clip_point_count{};
};

struct prepared_clip_request64 final {
    prepared_clip_request64() = default;
    prepared_clip_request64(const prepared_clip_request64&) = default;
    CLIPPER2NEXT_API auto operator=(const prepared_clip_request64&) -> prepared_clip_request64&;
    CLIPPER2NEXT_API prepared_clip_request64(prepared_clip_request64&&) noexcept;
    CLIPPER2NEXT_API auto operator=(prepared_clip_request64&&) noexcept -> prepared_clip_request64&;

    [[nodiscard]] auto request() const noexcept -> const clip_request64& { return request_; }
    [[nodiscard]] auto metadata() const noexcept -> const clip_request_metadata64& {
        return metadata_;
    }

private:
    struct runtime_data;

    auto swap(prepared_clip_request64& other) noexcept -> void;
    auto reset() noexcept -> void;

    friend CLIPPER2NEXT_API auto prepare_clip_request(clip_request64 request)
        -> prepared_clip_request64;
    friend CLIPPER2NEXT_API auto clip(const prepared_clip_request64& request)
        -> paths64_result;
    friend CLIPPER2NEXT_API auto clip_into(const prepared_clip_request64& request,
                                           paths64_result& result) -> void;
    friend CLIPPER2NEXT_API auto clip_checked(const prepared_clip_request64& request)
        -> expected_paths64_result;

    prepared_clip_request64(clip_request64 request,
                            clip_request_metadata64 metadata,
                            std::shared_ptr<const runtime_data> runtime_data)
        : request_(std::move(request)),
          metadata_(std::move(metadata)),
          runtime_data_(std::move(runtime_data)) {}

    clip_request64 request_{};
    clip_request_metadata64 metadata_{};
    std::shared_ptr<const runtime_data> runtime_data_{};
};

}  // namespace clipper2next
