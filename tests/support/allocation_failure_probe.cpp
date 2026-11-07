#include "support/allocation_failure_probe.h"

#include <atomic>
#include <cstdlib>
#include <new>

#if defined(_MSC_VER) && !defined(CLIPPER2NEXT_NO_ALLOCATION_CONTRACT)
static_assert(_ITERATOR_DEBUG_LEVEL == 0,
              "Global OOM injection must not target noexcept checked-iterator proxy allocations");
#endif

namespace {
using clipper2next::tests::allocation_failure_family;
std::atomic<std::ptrdiff_t> allocationsBeforeFailure{-1};
std::atomic<std::size_t> rejectedAllocations{0};
std::atomic<allocation_failure_family> failedFamily{allocation_failure_family::none};
std::atomic<clipper2next::tests::deallocation_callback> deallocationCallback{nullptr};

[[nodiscard]] auto shouldFailAllocation(allocation_failure_family family) noexcept -> bool {
#if defined(CLIPPER2NEXT_NO_ALLOCATION_CONTRACT)
    if (allocationsBeforeFailure.load(std::memory_order_relaxed) != 0) { return false; }
    rejectedAllocations.fetch_add(1, std::memory_order_relaxed);
    failedFamily.store(family, std::memory_order_relaxed);
    return true;
#else
    auto remaining = allocationsBeforeFailure.load(std::memory_order_relaxed);
    while (remaining >= 0) {
        if (allocationsBeforeFailure.compare_exchange_weak(
                remaining, remaining - 1, std::memory_order_relaxed, std::memory_order_relaxed)) {
            if (remaining != 0) { return false; }
            rejectedAllocations.fetch_add(1, std::memory_order_relaxed);
            failedFamily.store(family, std::memory_order_relaxed);
            return true;
        }
    }
    return false;
#endif
}
}  // namespace

namespace clipper2next::tests {
ScopedAllocationFailure::ScopedAllocationFailure(std::ptrdiff_t successful_allocations) noexcept {
    rejectedAllocations.store(0, std::memory_order_relaxed);
    failedFamily.store(allocation_failure_family::none, std::memory_order_relaxed);
    allocationsBeforeFailure.store(successful_allocations, std::memory_order_relaxed);
}
ScopedAllocationFailure::~ScopedAllocationFailure() {
    allocationsBeforeFailure.store(-1, std::memory_order_relaxed);
}
auto rejected_allocation_count() noexcept -> std::size_t {
    return rejectedAllocations.load(std::memory_order_relaxed);
}
auto allocations_before_failure() noexcept -> std::ptrdiff_t {
    return allocationsBeforeFailure.load(std::memory_order_relaxed);
}
auto failed_allocation_family() noexcept -> allocation_failure_family {
    return failedFamily.load(std::memory_order_relaxed);
}

auto set_deallocation_callback(deallocation_callback callback) noexcept -> deallocation_callback {
    return deallocationCallback.exchange(callback, std::memory_order_relaxed);
}
}  // namespace clipper2next::tests
void* operator new(const std::size_t size) {
    if (shouldFailAllocation(allocation_failure_family::throwing)) { throw std::bad_alloc{}; }
    if (auto* const result = std::malloc(size); result != nullptr) { return result; }
    throw std::bad_alloc{};
}

void* operator new[](const std::size_t size) {
    return ::operator new(size);
}

void* operator new(const std::size_t size, const std::nothrow_t&) noexcept {
    if (shouldFailAllocation(allocation_failure_family::nothrow)) { return nullptr; }
    return std::malloc(size);
}

void* operator new[](const std::size_t size, const std::nothrow_t& tag) noexcept {
    return ::operator new(size, tag);
}

void operator delete(void* const pointer) noexcept {
    if (const auto callback = deallocationCallback.load(std::memory_order_relaxed)) { callback(pointer); }
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
    ::operator delete(pointer);
}

void operator delete[](void* const pointer, const std::nothrow_t& tag) noexcept {
    ::operator delete(pointer, tag);
}
