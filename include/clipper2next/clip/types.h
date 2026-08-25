// SPDX-License-Identifier: BSL-1.0

#pragma once

#include "clipper2next/core.h"

namespace clipper2next {

enum class ClipType { NoClip, Intersection, Union, Difference, Xor };
enum class PathType { Subject, Clip };

}  // namespace clipper2next
