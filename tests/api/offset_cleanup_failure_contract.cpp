#include "clipper2next/offset.h"
#include "clipper2next/core/path_set_builder.h"
#include "support/allocation_failure_probe.h"

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <new>

namespace {
std::ptrdiff_t activeOrdinal{-1};
const char* activePhase = "setup";
using clipper2next::tests::ScopedAllocationFailure;
}  // namespace
int main() {
    std::set_terminate([] {
        std::fprintf(stderr, "terminated: ordinal=%td phase=%s remaining=%td\n",
                     activeOrdinal, activePhase, clipper2next::tests::allocations_before_failure());
        if (const auto failure = std::current_exception()) {
            try { std::rethrow_exception(failure); }
            catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); }
            catch (...) { std::fprintf(stderr, "non-standard exception\n"); }
        }
        std::_Exit(90);
    });
    namespace next = clipper2next;
    // begin reserves descriptors then resizes points. Both allocation failures
    // leave logical contents empty, without requiring active destruction.
    for (const auto ordinal : {std::ptrdiff_t{0}, std::ptrdiff_t{1}}) {
        auto owner = next::path_set64{};
        auto builder = next::path_set_builder64{owner};
        auto allocationFailed = false;
        {
            const auto failure = ScopedAllocationFailure{ordinal};
            try { builder.begin(2, 2); }
            catch (const std::bad_alloc&) { allocationFailed = true; }
        }
        if (!allocationFailed || !owner.empty() || owner.point_count() != 0) { return 40; }
    }
    const auto source = next::Paths64{
        next::Path64{{0, 0}, {100, 0}, {100, 100}, {0, 100}},
    };
    auto request_group = next::borrowed_offset_group64{};
    auto request = next::borrowed_offset_request64{};
    request.groups = std::span{&request_group, 1U};
    request_group.paths = next::borrow_paths64(source);
    request.delta = -5.0;
    request_group.join_type = next::JoinType::Miter;
    request_group.end_type = next::EndType::Polygon;

    const auto reference = next::offset_stage_checked(request);
    if (!reference || reference->paths.empty()) { return 10; }

    auto observedAllocationFailure = false;
    for (auto ordinal = std::ptrdiff_t{}; ordinal < 256; ++ordinal) {
        activeOrdinal = ordinal;
        auto result = next::expected_borrowed_offset_stage_result64{};
        {
            const auto failure = ScopedAllocationFailure{ordinal};
            activePhase = "API invocation / assignment";
            result = next::offset_stage_checked(request);
            activePhase = "API returned";
        }
        if (result && result->paths.empty()) { return 20; }
        if (!result && result.error() == next::clipper_error_code::allocation_failure) {
            observedAllocationFailure = true;
        }
    }
    if (!observedAllocationFailure) { return 30; }

    auto owning = next::offset_request64{};
    owning.paths = source;
    owning.delta = -5.0;
    observedAllocationFailure = false;
    for (auto ordinal = std::ptrdiff_t{}; ordinal < 256; ++ordinal) {
        activeOrdinal = ordinal;
        auto result = next::expected_paths64_result{};
        {
            const auto failure = ScopedAllocationFailure{ordinal};
            activePhase = "owning checked invocation / assignment";
            result = next::offset_checked(owning);
            activePhase = "owning checked returned";
        }
        if (result && result->closed.empty()) { return 50; }
        if (!result && result.error() == next::clipper_error_code::allocation_failure) {
            observedAllocationFailure = true;
        }
    }
    return observedAllocationFailure ? 0 : 60;
}
