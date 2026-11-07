#include "clipper2next/clip.h"
#include "clipper2next/rectclip.h"
#include "support/allocation_failure_probe.h"

#include <cstddef>
#include <cstdio>
#include <new>
#include <utility>

namespace next = clipper2next;
using next::tests::ScopedAllocationFailure;

namespace {

auto same_request(const next::clip_request64& a, const next::clip_request64& b) -> bool {
    return a.clip_type == b.clip_type && a.fill_rule == b.fill_rule &&
           a.options.preserve_collinear == b.options.preserve_collinear &&
           a.options.reverse_solution == b.options.reverse_solution &&
           a.options.intersection_policy.mode == b.options.intersection_policy.mode &&
           a.subjects == b.subjects && a.open_subjects == b.open_subjects && a.clips == b.clips;
}

auto same_metadata(const next::clip_request_metadata64& a,
                   const next::clip_request_metadata64& b) -> bool {
    return a.single_subject_rect == b.single_subject_rect &&
           a.single_clip_rect == b.single_clip_rect &&
           a.subject_path_count == b.subject_path_count &&
           a.open_subject_path_count == b.open_subject_path_count &&
           a.clip_path_count == b.clip_path_count &&
           a.subject_point_count == b.subject_point_count &&
           a.open_subject_point_count == b.open_subject_point_count &&
           a.clip_point_count == b.clip_point_count;
}

auto make_request(std::int64_t x, bool extra_point) -> next::clip_request64 {
    next::clip_request64 request;
    request.clip_type = next::ClipType::Union;
    request.fill_rule = next::FillRule::NonZero;
    request.subjects = {{{x, 0}, {x + 10, 0}, {x + 10, 10}, {x, 10}}};
    if (extra_point) {
        request.subjects.front().insert(request.subjects.front().begin() + 1, {x + 5, 0});
    }
    request.open_subjects = {{{x, 20}, {x + 10, 20}}};
    request.clips = {{{x + 20, 0}, {x + 30, 0}, {x + 30, 10}, {x + 20, 10}}};
    if (extra_point) {
        request.open_subjects.front().push_back({x + 20, 20});
        request.clips.push_back({{x + 40, 0}, {x + 50, 0}, {x + 50, 10}, {x + 40, 10}});
    }
    return request;
}

auto clip_copy_failures() -> bool {
    const auto old = next::prepare_clip_request(make_request(0, false));
    const auto replacement = next::prepare_clip_request(make_request(100, true));
    const auto old_result = next::clip(old);
    const auto new_result = next::clip(replacement);
    if (old_result.closed.empty() || new_result.closed.empty() ||
        old_result.closed == new_result.closed) { return false; }
    bool invariant_held = true;
    std::ptrdiff_t failures = 0;
    for (std::ptrdiff_t ordinal = 0; ordinal < 256; ++ordinal) {
        auto target = old;
        bool failed = false;
        {
            const ScopedAllocationFailure failure{ordinal};
            try { target = replacement; }
            catch (const std::bad_alloc&) { failed = true; }
        }
        const auto& expected = failed ? old : replacement;
        const auto& result_expected = failed ? old_result : new_result;
        const auto result = next::clip(target);
        if (!same_request(target.request(), expected.request()) ||
            !same_metadata(target.metadata(), expected.metadata()) ||
            result.closed != result_expected.closed || result.open != result_expected.open) {
            std::fprintf(stderr, "clip copy snapshot changed on failure ordinal=%td\n", ordinal);
            invariant_held = false;
        }
        if (!failed) {
            std::printf("clip copy: %td failing allocation ordinals, first success=%td\n",
                        failures, ordinal);
            return failures > 0 && invariant_held;
        }
        ++failures;
    }
    return false;
}

auto rect_copy_failures() -> bool {
    const auto old = next::prepare_rect_clip_request(
        {{-5, -5, 40, 40}, make_request(0, false).subjects});
    auto new_paths = make_request(100, true).subjects;
    new_paths.push_back({{120, 0}, {130, 0}, {130, 10}, {120, 10}});
    const auto replacement = next::prepare_rect_clip_request(
        {{95, -5, 140, 40}, std::move(new_paths)});
    const auto old_result = next::rect_clip(old);
    const auto new_result = next::rect_clip(replacement);
    if (old_result.paths.empty() || new_result.paths.empty() ||
        old_result.paths == new_result.paths) { return false; }
    bool invariant_held = true;
    std::ptrdiff_t failures = 0;
    for (std::ptrdiff_t ordinal = 0; ordinal < 256; ++ordinal) {
        auto target = old;
        bool failed = false;
        {
            const ScopedAllocationFailure failure{ordinal};
            try { target = replacement; }
            catch (const std::bad_alloc&) { failed = true; }
        }
        const auto& expected = failed ? old : replacement;
        const auto& result_expected = failed ? old_result : new_result;
        if (target.request().rect != expected.request().rect ||
            target.request().paths != expected.request().paths ||
            next::rect_clip(target).paths != result_expected.paths) {
            std::fprintf(stderr, "rect copy snapshot changed on failure ordinal=%td\n", ordinal);
            invariant_held = false;
        }
        if (!failed) {
            std::printf("rect copy: %td failing allocation ordinals, first success=%td\n",
                        failures, ordinal);
            return failures > 0 && invariant_held;
        }
        ++failures;
    }
    return false;
}

}  // namespace

int main() {
    std::printf("OOM ABI prepared clip size=%zu align=%zu rect size=%zu align=%zu\n",
                sizeof(next::prepared_clip_request64), alignof(next::prepared_clip_request64),
                sizeof(next::prepared_rect_clip_request64),
                alignof(next::prepared_rect_clip_request64));
    const auto clip_ok = clip_copy_failures();
    const auto rect_ok = rect_copy_failures();
    return clip_ok && rect_ok ? 0 : 1;
}
