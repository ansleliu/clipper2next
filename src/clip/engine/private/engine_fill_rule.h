#pragma once

#include "clipper2next/clip/types.h"

namespace clipper2next::internal {

constexpr bool is_primary_boundary(FillRule fill_rule, int wind_count) noexcept {
    switch (fill_rule) {
    case FillRule::EvenOdd: {
        return true;
    }
    case FillRule::NonZero: {
        return wind_count == 1 || wind_count == -1;
    }
    case FillRule::Positive: {
        return wind_count == 1;
    }
    case FillRule::Negative: {
        return wind_count == -1;
    }
    }
    return false;
}

constexpr bool is_inside_fill(FillRule fill_rule, int wind_count) noexcept {
    switch (fill_rule) {
    case FillRule::Positive: {
        return wind_count > 0;
    }
    case FillRule::Negative: {
        return wind_count < 0;
    }
    case FillRule::EvenOdd:
    case FillRule::NonZero: {
        return wind_count != 0;
    }
    }
    return false;
}

constexpr bool is_outside_fill(FillRule fill_rule, int wind_count) noexcept {
    switch (fill_rule) {
    case FillRule::Positive: {
        return wind_count <= 0;
    }
    case FillRule::Negative: {
        return wind_count >= 0;
    }
    case FillRule::EvenOdd:
    case FillRule::NonZero: {
        return wind_count == 0;
    }
    }
    return false;
}

constexpr bool is_contributing_closed(ClipType clip_type,
                                      FillRule fill_rule,
                                      PathType path_type,
                                      int wind_count,
                                      int opposite_wind_count) noexcept {
    if (!is_primary_boundary(fill_rule, wind_count)) { return false; }

    switch (clip_type) {
    case ClipType::NoClip: {
        return false;
    }
    case ClipType::Intersection: {
        return is_inside_fill(fill_rule, opposite_wind_count);
    }
    case ClipType::Union: {
        return is_outside_fill(fill_rule, opposite_wind_count);
    }
    case ClipType::Difference: {
        const bool subject_result = is_outside_fill(fill_rule, opposite_wind_count);
        return path_type == PathType::Subject ? subject_result : !subject_result;
    }
    case ClipType::Xor: {
        return true;
    }
    }
    return false;
}

constexpr bool is_contributing_open(ClipType clip_type,
                                    FillRule fill_rule,
                                    int wind_count,
                                    int opposite_wind_count) noexcept {
    const bool is_in_clip = is_inside_fill(fill_rule, opposite_wind_count);
    const bool is_in_subject = is_inside_fill(fill_rule, wind_count);

    switch (clip_type) {
    case ClipType::Intersection: {
        return is_in_clip;
    }
    case ClipType::Union: {
        return !is_in_subject && !is_in_clip;
    }
    case ClipType::NoClip:
    case ClipType::Difference:
    case ClipType::Xor: {
        return !is_in_clip;
    }
    }
    return false;
}

}  // namespace clipper2next::internal
