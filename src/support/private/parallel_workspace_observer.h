#pragma once

#include <array>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <span>
#include <utility>

namespace clipper2next::internal {

class parallel_engine_workspace_observer;
class scoped_workspace_publication;

// One child ledger publishes every accepted owner transition through its slot.
// Slot storage belongs to the synchronous frame; binding never allocates.
class parallel_engine_workspace_slot final {
public:
    auto change(std::size_t previous, std::size_t next) noexcept -> void;
    auto reclaim(std::size_t previous, void* storage,
                 void (*clear)(void*) noexcept) noexcept -> void;

private:
    friend class parallel_engine_workspace_observer;
    friend class scoped_workspace_publication;
    auto begin_publication() noexcept -> void;
    auto end_publication() noexcept -> void;
    class gate final {
    public:
        auto lock() noexcept -> void;
        auto unlock() noexcept -> void;
        auto request_waiter() noexcept -> void;
        [[nodiscard]] auto has_waiter() const noexcept -> bool {
            return state_.load(std::memory_order_relaxed) == 2U;
        }
    private:
        // 0 is free, 1 is held, 2 is held with a waiting participant.
        std::atomic<unsigned> state_{};
    };
    gate exclusion_;
    bool publishing_{};
    std::size_t live_{};
    parallel_engine_workspace_observer* observer_{};
    // Separate owners' mutable state by a full cache-line gap without
    // imposing extended alignment on every enclosing automatic object.
    std::array<std::byte, 64> separation_{};
};

// One single-writer phase may only grow effective storage. Every successful
// prefix is still published immediately. A pending observer may inspect a
// completed prefix; after its snapshot, this phase resumes batched publication.
// Callers must end the scope before decreasing storage or querying the observer.
class scoped_workspace_publication final {
public:
    explicit scoped_workspace_publication(parallel_engine_workspace_slot* slot) noexcept
        : slot_(slot) {
        if (slot_) { slot_->begin_publication(); }
    }
    scoped_workspace_publication(const scoped_workspace_publication&) = delete;
    auto operator=(const scoped_workspace_publication&) -> scoped_workspace_publication& = delete;
    ~scoped_workspace_publication() { release(); }
    auto release() noexcept -> void {
        if (auto* slot = std::exchange(slot_, nullptr)) { slot->end_publication(); }
    }
private:
    parallel_engine_workspace_slot* slot_{};
};

// Positive transitions update only their child. Before any decrease, exclude
// every child and record the real simultaneous total. While acquiring slots,
// all other decreases are excluded and the total can only increase. The frozen
// total therefore covers every earlier maximum since the preceding decrease.
class parallel_engine_workspace_observer final {
public:
    explicit parallel_engine_workspace_observer(std::span<parallel_engine_workspace_slot> slots) noexcept;
    parallel_engine_workspace_observer(const parallel_engine_workspace_observer&) = delete;
    auto operator=(const parallel_engine_workspace_observer&) -> parallel_engine_workspace_observer& = delete;
    ~parallel_engine_workspace_observer();
    [[nodiscard]] auto live() noexcept -> std::size_t;
    [[nodiscard]] auto peak() noexcept -> std::size_t;
    auto reset_after_transfer() noexcept -> void;

private:
    friend class parallel_engine_workspace_slot;
    auto change(parallel_engine_workspace_slot& slot,
                std::size_t previous, std::size_t next) noexcept -> void;
    auto reclaim(parallel_engine_workspace_slot& slot, std::size_t previous,
                 void* storage, void (*clear)(void*) noexcept) noexcept -> void;
    auto wait_for_snapshot(parallel_engine_workspace_slot& slot) noexcept -> void;
    [[nodiscard]] auto freeze() noexcept -> std::size_t;
    auto thaw() noexcept -> void;
    parallel_engine_workspace_slot::gate decreases_;
    // Odd epochs identify a snapshot; the following even epoch completes it.
    std::atomic<unsigned> snapshot_epoch_{};
    std::span<parallel_engine_workspace_slot> slots_;
    std::size_t completed_peak_{};
};

inline auto parallel_engine_workspace_slot::change(
    std::size_t previous, std::size_t next) noexcept -> void {
    assert(observer_ != nullptr);
    if (previous == next) { return; }
    if (publishing_ && next > previous) {
        live_ += next - previous;
        if (exclusion_.has_waiter()) { observer_->wait_for_snapshot(*this); }
    } else {
        observer_->change(*this, previous, next);
    }
}

} // namespace clipper2next::internal
