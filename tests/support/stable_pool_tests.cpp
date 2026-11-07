#include "support/private/storage/stable_pool.h"
#include "support/private/storage/stable_block_storage.h"

#include <gtest/gtest.h>
#include <array>

namespace next = clipper2next;

TEST(Clipper2NextStablePoolTests, KeepsReferencesStableAcrossInsertions) {
    next::internal::stable_pool<int> pool;

    int& first = pool.emplace(7);
    auto* first_address = &first;
    for (int index = 0; index < 1024; ++index) { static_cast<void>(pool.emplace(index)); }

    EXPECT_EQ(first_address, &first);
    EXPECT_EQ(*first_address, 7);
    EXPECT_EQ(pool.size(), 1025U);

    std::array<next::internal::parallel_engine_workspace_slot, 1> slots{};
    auto observer = next::internal::parallel_engine_workspace_observer{std::span{slots}};
    auto root = next::internal::execution_resource_ledger{};
    root.parallel_observer = &slots[0];
    next::internal::stable_pool<int, 2> retained;
    auto* retained_address = &retained.emplace(1);
    static_cast<void>(retained.emplace(2));
    retained.clear();
    retained.bind(&root);
    root.maximum_workspace_bytes = sizeof(int) - 1U;
    EXPECT_THROW(static_cast<void>(retained.emplace_direct(7)), next::clipper_error);
    EXPECT_EQ(retained.size(), 0U);
    EXPECT_EQ(root.engine_high_water, 0U);
    root.maximum_workspace_bytes = 2U * sizeof(int);
    EXPECT_EQ(&retained.emplace_direct(7), retained_address);
    auto held = next::internal::scoped_workspace_reservation{&root};
    held.set_bytes(sizeof(int));
    EXPECT_THROW(static_cast<void>(retained.emplace_direct(8)), next::clipper_error);
    EXPECT_EQ(retained.size(), 1U);
    EXPECT_EQ(root.engine_bytes, sizeof(int));
    EXPECT_EQ(root.reserved_engine_bytes, sizeof(int));
    EXPECT_EQ(observer.peak(), sizeof(int));
    held.release();
    EXPECT_EQ(retained.emplace_direct(8), 8);
    root.maximum_workspace_bytes = 3U * sizeof(int);
    EXPECT_EQ(retained.emplace_direct(9), 9);
    EXPECT_EQ(retained.size(), 3U);
    EXPECT_EQ(root.engine_bytes, 3U * sizeof(int));
    EXPECT_EQ(root.engine_high_water, observer.peak());
    EXPECT_EQ(root.reserved_engine_bytes, 0U);
    retained.release();
    EXPECT_EQ(root.engine_bytes, 0U);
    EXPECT_EQ(observer.live(), 0U);
}

TEST(Clipper2NextStablePoolTests, ClearReleasesTrackedEntries) {
    next::internal::stable_pool<int> pool;
    static_cast<void>(pool.emplace(1));
    static_cast<void>(pool.emplace(2));

    pool.clear();

    EXPECT_TRUE(pool.empty());
    EXPECT_EQ(pool.size(), 0U);
}

TEST(Clipper2NextStablePoolTests, ReleaseReturnsRetainedBlocks) {
    next::internal::stable_pool<int, 2U> pool;
    static_cast<void>(pool.emplace(1));
    static_cast<void>(pool.emplace(2));
    static_cast<void>(pool.emplace(3));
    ASSERT_GE(pool.retained_capacity(), 3U);

    pool.release();

    EXPECT_EQ(pool.retained_capacity(), 0U);
}

#include <cstdint>
#include <stdexcept>
namespace {
struct alignas(64) tracked_pool_entry final {
    int* live;
    int value;
    std::array<std::byte, 64U - sizeof(int*) - sizeof(int)> padding{};
    tracked_pool_entry(int& count, int number, bool reject = false,
                       next::internal::stable_pool<int, 1>* other = nullptr) : live(&count), value(number) {
        if (other) { static_cast<void>(other->emplace(7)); }
        if (reject) { throw std::runtime_error("constructor rejected"); }
        ++*live;
    }
    tracked_pool_entry(const tracked_pool_entry&) = delete;
    ~tracked_pool_entry() { --*live; }
};
}
TEST(Clipper2NextStablePoolTests, NontrivialLifetimesAndFailedConstructionPreserveOwnerCharge) {
    std::array<next::internal::parallel_engine_workspace_slot, 1> observer_slots{};
    auto observer = next::internal::parallel_engine_workspace_observer{std::span{observer_slots}};
    auto root = next::internal::execution_resource_ledger{};
    root.parallel_observer = &observer_slots[0];
    int live = 0;
    {
        next::internal::stable_pool<tracked_pool_entry, 2> pool;
        pool.bind(&root);
        auto* first = &pool.emplace(live, 1);
        auto* second = &pool.emplace(live, 2);
        EXPECT_EQ(reinterpret_cast<std::uintptr_t>(first) % alignof(tracked_pool_entry), 0U);
        EXPECT_THROW(static_cast<void>(pool.emplace(live, 3, true)), std::runtime_error);
        EXPECT_EQ(pool.size(), 2U);
        EXPECT_EQ(live, 2);
        EXPECT_EQ(root.engine_bytes, 2U * sizeof(tracked_pool_entry));
        EXPECT_EQ(root.engine_high_water, 2U * sizeof(tracked_pool_entry));
        EXPECT_EQ(observer.peak(), 2U * sizeof(tracked_pool_entry));
        auto& third = pool.emplace(live, 3);
        EXPECT_EQ(third.value, 3);
        EXPECT_EQ(first->value, 1);
        EXPECT_EQ(second->value, 2);
        EXPECT_EQ(live, 3);
        EXPECT_EQ(root.engine_bytes, 3U * sizeof(tracked_pool_entry));
        pool.clear();
        EXPECT_EQ(live, 0);
        EXPECT_EQ(root.engine_bytes, 0U);
        auto& reused = pool.emplace(live, 4);
        EXPECT_EQ(&reused, first);
        EXPECT_EQ(reused.value, 4);
        EXPECT_EQ(root.engine_bytes, sizeof(tracked_pool_entry));
        pool.release();
        EXPECT_EQ(pool.retained_capacity(), 0U);
        EXPECT_EQ(live, 0);
        EXPECT_EQ(root.engine_bytes, 0U);
        static_cast<void>(pool.emplace(live, 5));
    }
    EXPECT_EQ(live, 0);
    EXPECT_EQ(root.engine_bytes, 0U);
    {
        next::internal::stable_pool<tracked_pool_entry, 2> pool;
        auto* first = &pool.emplace(live, 6);
        EXPECT_THROW(static_cast<void>(pool.emplace(live, 7, true)), std::runtime_error);
        EXPECT_EQ(pool.size(), 1U);
        EXPECT_EQ(live, 1);
        root.maximum_workspace_bytes = 0U;
        EXPECT_THROW(pool.bind(&root), next::clipper_error);
        EXPECT_NO_THROW(static_cast<void>(pool.emplace(live, 9)));
        EXPECT_EQ(root.engine_bytes, 0U);
        pool.clear();
        EXPECT_EQ(live, 0);
        first = &pool.emplace(live, 6);
        root.maximum_workspace_bytes = (std::numeric_limits<std::size_t>::max)();
        pool.bind(&root);
        EXPECT_EQ(root.engine_bytes, sizeof(tracked_pool_entry));
        pool.detach();
        EXPECT_EQ(root.engine_bytes, 0U);
        static_cast<void>(pool.emplace(live, 8));
        EXPECT_EQ(first->value, 6);
        EXPECT_EQ(live, 2);
        pool.bind(&root);
        EXPECT_EQ(root.engine_bytes, 2U * sizeof(tracked_pool_entry));
        EXPECT_EQ(observer.live(), root.engine_bytes);
        pool.clear();
        EXPECT_EQ(live, 0);
        EXPECT_EQ(root.engine_bytes, 0U);
        EXPECT_EQ(observer.live(), 0U);
    }
}

TEST(Clipper2NextStablePoolTests, FailedGrowthProtectsQuotaWithoutErasingAnotherOwnersPeak) {
    for (bool allow_other : {false, true}) {
        std::array<next::internal::parallel_engine_workspace_slot, 1> observer_slots{};
        auto observer = next::internal::parallel_engine_workspace_observer{std::span{observer_slots}};
        auto root = next::internal::execution_resource_ledger{};
        root.parallel_observer = &observer_slots[0];
        root.maximum_workspace_bytes = 3U * sizeof(tracked_pool_entry) + (allow_other ? sizeof(int) : 0U);
        int live = 0;
        {
            next::internal::stable_pool<int, 1> other;
            next::internal::stable_pool<tracked_pool_entry, 2> pool;
            other.bind(&root);
            pool.bind(&root);
            static_cast<void>(pool.emplace(live, 1));
            static_cast<void>(pool.emplace(live, 2));
            bool rejected = false;
            try { static_cast<void>(pool.emplace(live, 3, true, &other)); }
            catch (const next::clipper_error& error) {
                rejected = true;
                EXPECT_FALSE(allow_other);
                EXPECT_EQ(error.code(), next::clipper_error_code::resource_limit);
            } catch (const std::runtime_error&) {
                rejected = true;
                EXPECT_TRUE(allow_other);
            }
            EXPECT_TRUE(rejected);
            EXPECT_EQ(pool.size(), 2U);
            EXPECT_EQ(live, 2);
            EXPECT_EQ(other.size(), allow_other ? 1U : 0U);
            const auto actual = 2U * sizeof(tracked_pool_entry) + (allow_other ? sizeof(int) : 0U);
            EXPECT_EQ(root.engine_bytes, actual);
            EXPECT_EQ(root.engine_high_water, actual);
            EXPECT_EQ(observer.live(), actual);
            EXPECT_EQ(observer.peak(), actual);
            EXPECT_EQ(root.reserved_engine_bytes, 0U);
            EXPECT_EQ(root.reserved_staging_bytes, 0U);
        }
        EXPECT_EQ(root.engine_bytes, 0U);
        EXPECT_EQ(observer.live(), 0U);
        EXPECT_EQ(live, 0);
    }
}

TEST(Clipper2NextStablePoolTests, PhysicalStorageKeepsReferencesAndReusesRetainedBlocks) {
    next::internal::stable_block_storage<int, 2> storage;
    auto* first = &storage.emplace(7);
    for (int value = 0; value < 1024; ++value) {
        static_cast<void>(storage.emplace(value));
    }
    EXPECT_EQ(*first, 7);
    EXPECT_EQ(storage.size(), 1025U);
    const auto retained = storage.retained_capacity();
    storage.clear();
    EXPECT_TRUE(storage.empty());
    EXPECT_EQ(storage.retained_capacity(), retained);
    EXPECT_EQ(&storage.emplace_retained(9), first);
    EXPECT_EQ(*first, 9);
    storage.release();
    EXPECT_EQ(storage.size(), 0U);
    EXPECT_EQ(storage.retained_capacity(), 0U);
}

TEST(Clipper2NextStablePoolTests, PhysicalStoragePreservesNontrivialConstructionPrefix) {
    int live = 0;
    {
        next::internal::stable_block_storage<tracked_pool_entry, 2> storage;
        auto* first = &storage.emplace(live, 1);
        EXPECT_EQ(reinterpret_cast<std::uintptr_t>(first) % alignof(tracked_pool_entry), 0U);
        EXPECT_THROW(static_cast<void>(storage.emplace_retained(live, 2, true)), std::runtime_error);
        EXPECT_EQ(storage.size(), 1U);
        EXPECT_EQ(live, 1);
        EXPECT_EQ(first->value, 1);
        static_cast<void>(storage.emplace(live, 2));
        EXPECT_EQ(live, 2);
        storage.clear();
        EXPECT_EQ(live, 0);
        EXPECT_EQ(&storage.emplace(live, 3), first);
        EXPECT_EQ(first->value, 3);
    }
    EXPECT_EQ(live, 0);
}
