// SPDX-License-Identifier: BSL-1.0

#pragma once

#include "clipper2next/api/error.h"
#include "clipper2next/api/export.h"
#include "clipper2next/core.h"

namespace clipper2next {

struct minkowski_request64 {
    Path64 pattern{};
    Path64 path{};
    bool is_closed{true};
};

struct minkowski_requestd {
    PathD pattern{};
    PathD path{};
    bool is_closed{true};
    int decimal_precision{2};
};

[[nodiscard]] CLIPPER2NEXT_API auto minkowski_sum(
    const minkowski_request64& request) -> Paths64;
[[nodiscard]] CLIPPER2NEXT_API auto minkowski_sum(
    const minkowski_requestd& request) -> PathsD;
[[nodiscard]] CLIPPER2NEXT_API auto minkowski_sum_checked(
    const minkowski_request64& request)
    -> clipper_result<Paths64>;
[[nodiscard]] CLIPPER2NEXT_API auto minkowski_sum_checked(
    const minkowski_requestd& request)
    -> clipper_result<PathsD>;

[[nodiscard]] CLIPPER2NEXT_API auto minkowski_difference(
    const minkowski_request64& request) -> Paths64;
[[nodiscard]] CLIPPER2NEXT_API auto minkowski_difference(
    const minkowski_requestd& request) -> PathsD;
[[nodiscard]] CLIPPER2NEXT_API auto minkowski_difference_checked(
    const minkowski_request64& request)
    -> clipper_result<Paths64>;
[[nodiscard]] CLIPPER2NEXT_API auto minkowski_difference_checked(
    const minkowski_requestd& request)
    -> clipper_result<PathsD>;

}  // namespace clipper2next
