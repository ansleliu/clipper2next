#include "clipper2next/offset.h"
#include "clipper2next/core/path_set_builder.h"

#include <atomic>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <new>

#if defined(_MSC_VER)
static_assert(_ITERATOR_DEBUG_LEVEL == 0,
              "Global OOM injection must not target noexcept checked-iterator proxy allocations");
#endif

namespace {

std::atomic<std::ptrdiff_t> allocationsBeforeFailure{-1};
std::ptrdiff_t activeOrdinal{-1};
const char* activePhase = "setup";

[[nodiscard]] auto shouldFailAllocation() noexcept -> bool {
    auto remaining = allocationsBeforeFailure.load(std::memory_order_relaxed);
    while (remaining >= 0) {
        if (allocationsBeforeFailure.compare_exchange_weak(
                remaining, remaining - 1, std::memory_order_relaxed, std::memory_order_relaxed)) {
            return remaining == 0;
        }
    }
    return false;
}

class ScopedAllocationFailure final {
public:
    explicit ScopedAllocationFailure(const std::ptrdiff_t successfulAllocations) noexcept {
        allocationsBeforeFailure.store(successfulAllocations, std::memory_order_relaxed);
    }

    ScopedAllocationFailure(const ScopedAllocationFailure&) = delete;
    auto operator=(const ScopedAllocationFailure&) -> ScopedAllocationFailure& = delete;

    ~ScopedAllocationFailure() { allocationsBeforeFailure.store(-1, std::memory_order_relaxed); }
};

}  // namespace

void* operator new(const std::size_t size) {
    if (shouldFailAllocation()) { throw std::bad_alloc{}; }
    if (auto* const result = std::malloc(size); result != nullptr) { return result; }
    throw std::bad_alloc{};
}

void* operator new[](const std::size_t size) {
    return ::operator new(size);
}

void* operator new(const std::size_t size, const std::nothrow_t&) noexcept {
    if (shouldFailAllocation()) { return nullptr; }
    return std::malloc(size);
}

void* operator new[](const std::size_t size, const std::nothrow_t& tag) noexcept {
    return ::operator new(size, tag);
}

void operator delete(void* const pointer) noexcept {
    std::free(pointer);
}

void operator delete[](void* const pointer) noexcept {
    ::operator delete(pointer);
}

void operator delete(void* const pointer, std::size_t) noexcept {
    ::operator delete(pointer);
}

void operator delete[](void* const pointer, std::size_t) noexcept {
    ::operator delete(pointer);
}

void operator delete(void* const pointer, const std::nothrow_t&) noexcept {
    std::free(pointer);
}

void operator delete[](void* const pointer, const std::nothrow_t& tag) noexcept {
    ::operator delete(pointer, tag);
}

int main() {
    std::set_terminate([] {
        std::fprintf(stderr, "terminated: ordinal=%td phase=%s remaining=%td\n",
                     activeOrdinal, activePhase, allocationsBeforeFailure.load());
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
