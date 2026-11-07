#include "support/private/parallel_workspace_observer.h"

#include <algorithm>
#include <cassert>

namespace clipper2next::internal {

auto parallel_engine_workspace_slot::gate::lock() noexcept -> void {
    auto expected = unsigned{};
    if (state_.compare_exchange_strong(expected, 1U, std::memory_order_acquire)) { return; }
    while (state_.exchange(2U, std::memory_order_acquire) != 0U) {
        state_.wait(2U, std::memory_order_relaxed);
    }
}

auto parallel_engine_workspace_slot::gate::unlock() noexcept -> void {
    if (state_.exchange(0U, std::memory_order_release) == 2U) { state_.notify_one(); }
}

auto parallel_engine_workspace_slot::gate::request_waiter() noexcept -> void {
    auto expected = unsigned{1U};
    static_cast<void>(state_.compare_exchange_strong(
        expected, 2U, std::memory_order_relaxed));
}

parallel_engine_workspace_observer::parallel_engine_workspace_observer(
    std::span<parallel_engine_workspace_slot> slots) noexcept
    : slots_(slots) {
    for (auto& slot : slots_) {
        assert(slot.live_ == 0U && slot.observer_ == nullptr && !slot.publishing_);
        slot.observer_ = this;
    }
}

parallel_engine_workspace_observer::~parallel_engine_workspace_observer() {
    assert(live() == 0U);
    for (auto& slot : slots_) { slot.observer_ = nullptr; }
}

auto parallel_engine_workspace_observer::live() noexcept -> std::size_t {
    const auto value = freeze();
    thaw();
    return value;
}

auto parallel_engine_workspace_observer::peak() noexcept -> std::size_t {
    static_cast<void>(freeze());
    const auto value = completed_peak_;
    thaw();
    return value;
}

auto parallel_engine_workspace_observer::reset_after_transfer() noexcept -> void {
    static_cast<void>(freeze());
    for (auto& slot : slots_) { slot.live_ = 0U; }
    thaw();
}

auto parallel_engine_workspace_observer::change(
    parallel_engine_workspace_slot& slot, std::size_t previous, std::size_t next) noexcept -> void {
    assert(!slot.publishing_);
    if (next > previous) {
        slot.exclusion_.lock();
        slot.live_ += next - previous;
        slot.exclusion_.unlock();
    } else {
        static_cast<void>(freeze());
        assert(slot.live_ >= previous - next);
        slot.live_ -= previous - next;
        thaw();
    }
}

auto parallel_engine_workspace_observer::wait_for_snapshot(
    parallel_engine_workspace_slot& slot) noexcept -> void {
    // Wait for this snapshot's completion, even if a later snapshot starts
    // before the publisher runs again. The slot gate still excludes that reader.
    const auto epoch = snapshot_epoch_.load(std::memory_order_acquire);
    slot.exclusion_.unlock();
    if (epoch & 1U) { snapshot_epoch_.wait(epoch, std::memory_order_acquire); }
    slot.exclusion_.lock();
}


auto parallel_engine_workspace_observer::reclaim(
    parallel_engine_workspace_slot& slot, std::size_t previous,
    void* storage, void (*clear)(void*) noexcept) noexcept -> void {
    assert(!slot.publishing_);
    static_cast<void>(freeze());
    assert(slot.live_ >= previous);
    // Storage and its local owners end inside the same excluded transition.
    // The concrete clear action cannot allocate, throw or reenter this frame.
    clear(storage);
    slot.live_ -= previous;
    thaw();
}

auto parallel_engine_workspace_observer::freeze() noexcept -> std::size_t {
    decreases_.lock();
    // Epoch writers are already serialized by decreases_.
    snapshot_epoch_.store(snapshot_epoch_.load(std::memory_order_relaxed) + 1U,
                          std::memory_order_release);
    // Ask every active publisher to yield before waiting for the first one.
    // This is only intent: the snapshot still acquires and retains every gate.
    for (auto& slot : slots_) { slot.exclusion_.request_waiter(); }
    // A positive writer releases its slot before waiting for completion.
    // Negative writers share this stable lock order, so no cycle exists.
    auto value = std::size_t{};
    for (auto& slot : slots_) {
        slot.exclusion_.lock();
        // Locked slots stay fixed. This is the simultaneous total when the
        // last slot is acquired; parent quota bounds the sum by SIZE_MAX.
        value += slot.live_;
    }
    completed_peak_ = (std::max)(completed_peak_, value);
    return value;
}

auto parallel_engine_workspace_observer::thaw() noexcept -> void {
    for (auto& slot : slots_) { slot.exclusion_.unlock(); }
    snapshot_epoch_.store(snapshot_epoch_.load(std::memory_order_relaxed) + 1U,
                          std::memory_order_release);
    snapshot_epoch_.notify_all();
    decreases_.unlock();
}

auto parallel_engine_workspace_slot::begin_publication() noexcept -> void {
    assert(observer_ != nullptr && !publishing_);
    exclusion_.lock();
    publishing_ = true;
}

auto parallel_engine_workspace_slot::end_publication() noexcept -> void {
    assert(publishing_);
    publishing_ = false;
    exclusion_.unlock();
}


auto parallel_engine_workspace_slot::reclaim(
    std::size_t previous, void* storage, void (*clear)(void*) noexcept) noexcept -> void {
    assert(observer_ != nullptr);
    observer_->reclaim(*this, previous, storage, clear);
}

} // namespace clipper2next::internal
