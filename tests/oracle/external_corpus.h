#pragma once

#include "clipper2/clipper.h"
#include "clipper2next/clipper.h"

#include <stdexcept>
#include <string>
#include <string_view>

namespace clipper2next::tests::oracle {

namespace legacy = Clipper2Lib;
namespace next = clipper2next;

enum class overlay_operation {
    intersection,
    union_,
    difference,
    xor_,
};

struct wkt_parser_options final {
    long double scale = 1.0L;
};

[[nodiscard]] inline auto to_next_clip_type(overlay_operation operation) -> next::ClipType {
    switch (operation) {
    case overlay_operation::intersection: {
        return next::ClipType::Intersection;
    }
    case overlay_operation::union_: {
        return next::ClipType::Union;
    }
    case overlay_operation::difference: {
        return next::ClipType::Difference;
    }
    case overlay_operation::xor_: {
        return next::ClipType::Xor;
    }
    }
    return next::ClipType::NoClip;
}

[[nodiscard]] inline auto to_legacy_clip_type(overlay_operation operation) -> legacy::ClipType {
    switch (operation) {
    case overlay_operation::intersection: {
        return legacy::ClipType::Intersection;
    }
    case overlay_operation::union_: {
        return legacy::ClipType::Union;
    }
    case overlay_operation::difference: {
        return legacy::ClipType::Difference;
    }
    case overlay_operation::xor_: {
        return legacy::ClipType::Xor;
    }
    }
    return legacy::ClipType::NoClip;
}

[[nodiscard]] inline auto parse_overlay_operation(std::string_view name) -> overlay_operation {
    if (name == "intersection") { return overlay_operation::intersection; }
    if (name == "union") { return overlay_operation::union_; }
    if (name == "difference") { return overlay_operation::difference; }
    if (name == "symdifference" || name == "symDifference" || name == "symmetric_difference" ||
        name == "xor") {
        return overlay_operation::xor_;
    }
    throw std::runtime_error{"unsupported overlay operation: " + std::string{name}};
}

}  // namespace clipper2next::tests::oracle
