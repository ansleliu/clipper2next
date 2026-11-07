#pragma once
#include "clipper2next/clip/request.h"
#include "support/allocation_failure_probe.h"
#include <array>
#include <cstdio>
#include <new>

#include "clip/private/closed_clip_fast_path.h"
#include "clipper2next/clip.h"
#include "clip/private/clip_request_metadata.h"
namespace clipper2next::tests {
inline auto warm_rectangle_fast_paths_allocate_nothing() -> bool {
    struct fixture {
        ClipType type;
        Rect64 first;
        Rect64 second;
        bool handled{true};
    };
    const std::array fixtures{
        fixture{ClipType::Intersection, {0, 0, 100, 100}, {50, 50, 150, 150}},
        fixture{ClipType::Intersection, {0, 0, 100, 100}, {200, 200, 300, 300}},
        fixture{ClipType::Union, {0, 0, 100, 100}, {0, 0, 100, 100}},
        fixture{ClipType::Union, {0, 0, 100, 100}, {100, 0, 200, 100}, false},
        fixture{ClipType::Union, {0, 0, 100, 100}, {0, 100, 100, 200}, false},
        fixture{ClipType::Union, {0, 0, 100, 100}, {50, 50, 150, 150}},
        fixture{ClipType::Union, {50, 50, 150, 150}, {0, 0, 100, 100}},
        fixture{ClipType::Union, {0, 50, 100, 150}, {50, 0, 150, 100}},
        fixture{ClipType::Union, {50, 0, 150, 100}, {0, 50, 100, 150}},
        fixture{ClipType::Union, {0, 0, 100, 100}, {200, 200, 300, 300}},
        fixture{ClipType::Union, {0, 0, 100, 100}, {100, 50, 200, 150}, false},
        fixture{ClipType::Difference, {0, 0, 100, 100}, {200, 200, 300, 300}},
        fixture{ClipType::Difference, {0, 0, 100, 100}, {0, 0, 100, 100}},
        fixture{ClipType::Difference, {0, 0, 100, 100}, {50, 50, 150, 150}},
        fixture{ClipType::Difference, {0, 0, 100, 100}, {50, -50, 150, 50}},
        fixture{ClipType::Difference, {0, 0, 100, 100}, {-50, -50, 50, 50}},
        fixture{ClipType::Difference, {0, 0, 100, 100}, {-50, 50, 50, 150}},
        fixture{ClipType::Xor, {0, 0, 100, 100}, {100, 0, 200, 100}, false},
        fixture{ClipType::Xor, {0, 0, 100, 100}, {0, 100, 100, 200}, false},
        fixture{ClipType::Xor, {0, 0, 100, 100}, {200, 200, 300, 300}},
        fixture{ClipType::Xor, {0, 0, 100, 100}, {50, 50, 150, 150}},
        fixture{ClipType::Xor, {0, 0, 100, 100}, {0, 0, 100, 100}, false}};
    bool passed = true;
    for (std::size_t index = 0; index < fixtures.size(); ++index) {
        const auto& item = fixtures[index];
        clip_request64 request;
        request.clip_type = item.type;
        request.subjects = {as_path(item.first)};
        request.clips = {as_path(item.second)};
        const auto metadata = internal::build_clip_request_metadata(request);
        paths64_result result;
        result.closed.resize(2);
        result.closed.reserve(1000);
        for (auto& path : result.closed) {
            path.reserve(10000);
            path.push_back({999, 999});
        }
        result.open = {{{7, 8}, {9, 10}}};
        const auto expected_handled = internal::try_execute_closed_clip_fast_path(request, metadata, result);
        if (expected_handled != item.handled) {
            std::fprintf(stderr, "rectangle fixture %zu: wrong fast-path eligibility\n", index);
            passed = false;
        }
        const auto expected = result;
        bool handled = false, threw = false;
        {
            ScopedAllocationFailure failure{0};
            try {
                handled = internal::try_execute_closed_clip_fast_path(request, metadata, result);
            } catch (const std::bad_alloc&) { threw = true; }
        }
        const auto hits = rejected_allocation_count();
        if (threw || hits != 0 || handled != expected_handled || result.closed != expected.closed ||
            result.open != expected.open) {
            std::fprintf(
                stderr, "warm rectangle fixture %zu: threw=%d rejected=%zu\n", index, threw, hits);
            passed = false;
        }


    }
    if (passed) {
        std::puts(
            "22 native rectangle dispatch fixtures: zero allocations, complete output unchanged");
    }
    return passed;
}
}  // namespace clipper2next::tests
