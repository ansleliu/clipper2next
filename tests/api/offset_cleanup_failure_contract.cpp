#include "clipper2next/offset.h"

#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <new>

namespace {

std::atomic<std::ptrdiff_t> allocationsBeforeFailure{-1};

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
    namespace next = clipper2next;
    const auto source = next::Paths64{
        next::Path64{{0, 0}, {100, 0}, {100, 100}, {0, 100}},
    };
    auto request = next::borrowed_offset_request64{};
    request.paths = next::borrow_paths64(source);
    request.delta = -5.0;
    request.join_type = next::JoinType::Miter;
    request.end_type = next::EndType::Polygon;

    const auto reference = next::offset_stage_checked(request);
    if (!reference || reference->paths.empty()) { return 10; }

    auto observedAllocationFailure = false;
    for (auto ordinal = std::ptrdiff_t{}; ordinal < 256; ++ordinal) {
        auto result = next::expected_borrowed_offset_stage_result64{};
        {
            const auto failure = ScopedAllocationFailure{ordinal};
            result = next::offset_stage_checked(request);
        }
        if (result && result->paths.empty()) { return 20; }
        if (!result && result.error() == next::clipper_error_code::allocation_failure) {
            observedAllocationFailure = true;
        }
    }
    return observedAllocationFailure ? 0 : 30;
}
