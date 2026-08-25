#pragma once

#include "clipper2next/api/error.h"
#include "clipper2next/core.h"
#include "clipper2next/polygon/poly_tree.h"

namespace clipper2next {

struct paths64_result final {
    Paths64 closed;
    Paths64 open;
};

struct clip_tree64_result final {
    PolyTree64 tree;
    Paths64 open;
};

struct rect_clip_result64 final {
    Paths64 paths;
};

using expected_paths64_result = clipper_result<paths64_result>;
using expected_clip_tree64_result = clipper_result<clip_tree64_result>;
using expected_rect_clip_result64 = clipper_result<rect_clip_result64>;

}  // namespace clipper2next
