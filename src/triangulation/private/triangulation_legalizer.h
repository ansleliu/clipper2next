#pragma once

#include "clipper2next/core.h"

namespace clipper2next::internal {

[[nodiscard]] auto LeftTurning(const Point64& p1, const Point64& p2, const Point64& p3) -> bool;

[[nodiscard]] auto RightTurning(const Point64& p1, const Point64& p2, const Point64& p3) -> bool;

[[nodiscard]] auto InCircleTest(const Point64& ptA,
                                const Point64& ptB,
                                const Point64& ptC,
                                const Point64& ptD) -> double;

}  // namespace clipper2next::internal
