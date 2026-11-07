#pragma once

#include <cstddef>

namespace clipper2next::tests {

enum class allocation_failure_family { none, throwing, nothrow };

// Linked only into standalone fault-injection executables, never product code.
class ScopedAllocationFailure final {
public:
    explicit ScopedAllocationFailure(std::ptrdiff_t successful_allocations) noexcept;
    ScopedAllocationFailure(const ScopedAllocationFailure&) = delete;
    auto operator=(const ScopedAllocationFailure&) -> ScopedAllocationFailure& = delete;
    ~ScopedAllocationFailure();
};

[[nodiscard]] auto rejected_allocation_count() noexcept -> std::size_t;
[[nodiscard]] auto allocations_before_failure() noexcept -> std::ptrdiff_t;
// Reset on each scope; identifies the last actual rejection (one per ordinal
// scope, potentially several in the persistent no-allocation contract).
[[nodiscard]] auto failed_allocation_family() noexcept -> allocation_failure_family;

using deallocation_callback = void (*)(void*) noexcept;
// Observes actual deletion in standalone contracts, including allocator reentry.
auto set_deallocation_callback(deallocation_callback callback) noexcept -> deallocation_callback;

}  // namespace clipper2next::tests
