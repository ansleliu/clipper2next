#include "clip/engine/private/engine_buffer.h"
#include <gtest/gtest.h>
#include <array>
#include <vector>

namespace in = clipper2next::internal;

TEST(Clipper2NextEngineBufferTests, UnboundShrinkAndLateBindingKeepValuesCapacityAndQuota) {
    in::engine_buffer<int64_t> values;
    values.reserve(8);
    auto next = int64_t{1};
    values.append_generated(4, [&next]() noexcept { return next++; });
    const auto capacity = values.capacity();
    values.pop_back();
    values.erase(values.begin() + 1, values.begin() + 2);
    EXPECT_EQ((std::vector<int64_t>{values.begin(), values.end()}), (std::vector<int64_t>{1,3}));
    in::execution_resource_ledger root;
    root.maximum_workspace_bytes = 0U;
    EXPECT_THROW(values.bind(&root), clipper2next::clipper_error);
    EXPECT_NO_THROW(values.push_back(99));
    EXPECT_EQ(root.engine_bytes, 0U);
    values.pop_back();
    root.maximum_workspace_bytes = 2U * sizeof(int64_t);
    values.bind(&root);
    EXPECT_EQ(root.engine_bytes, 2U * sizeof(int64_t));
    EXPECT_THROW(values.push_back(9), clipper2next::clipper_error);
    EXPECT_EQ(values.size(), 2U);
    values.pop_back();
    EXPECT_EQ(root.engine_bytes, sizeof(int64_t));
    auto generated = int64_t{9};
    EXPECT_THROW(values.append_generated(3, [&generated]() noexcept { return generated++; }),
                 clipper2next::clipper_error);
    EXPECT_EQ((std::vector<int64_t>{values.begin(), values.end()}), (std::vector<int64_t>{1,9}));
    EXPECT_EQ(root.engine_bytes, 2U * sizeof(int64_t));
    EXPECT_EQ(generated, 10);
    values.pop_back();
    values.detach();
    EXPECT_EQ(root.engine_bytes, 0U);
    values.erase(values.begin(), values.end());
    values.push_back(7);
    values.bind(&root);
    EXPECT_EQ(root.engine_bytes, sizeof(int64_t));
    values.clear();
    EXPECT_EQ(root.engine_bytes, 0U);
    const std::array<int64_t, 3> source{21,22,23};
    EXPECT_THROW(values.append_range(source), clipper2next::clipper_error);
    EXPECT_EQ((std::vector<int64_t>{values.begin(), values.end()}), (std::vector<int64_t>{21,22}));
    EXPECT_EQ(root.engine_bytes, 2U * sizeof(int64_t));
    values.clear();
    values.append_range(std::span{source}.first(2));
    EXPECT_EQ((std::vector<int64_t>{values.begin(), values.end()}), (std::vector<int64_t>{21,22}));
    values.clear();
    EXPECT_EQ(root.engine_bytes, 0U);
    EXPECT_EQ(root.engine_high_water, 2U * sizeof(int64_t));
    EXPECT_EQ(values.capacity(), capacity);
}

TEST(Clipper2NextEngineBufferTests, BoundShrinkKeepsForeignOwnersReservationsAndExactPeak) {
    const auto exercise = []<in::workspace_category category>() {
        for (const auto preallocated : {false, true}) {
            std::array<in::parallel_engine_workspace_slot, 2> slots{};
            in::parallel_engine_workspace_observer observer{slots};
            in::execution_resource_ledger root, other_root;
            root.parallel_observer = &slots[0];
            other_root.parallel_observer = &slots[1];
            in::scoped_workspace_charge foreign{&root, category}, other{&other_root};
            foreign.set_live_slots(12, 1);
            other.set_live_slots(16, 1);
            in::scoped_workspace_reservation reservation{&root, category};
            reservation.set_bytes(8);
            in::engine_buffer<int64_t, category> values;
            values.bind(&root);
            if (preallocated) { values.reserve(4); }
            auto next = int64_t{1};
            values.append_generated(4, [&next]() noexcept { return next++; });
            const auto result = values.erase(values.begin() + 1, values.begin() + 3);
            ASSERT_NE(result, values.end());
            EXPECT_EQ(*result, 4);
            values.pop_back();
            EXPECT_EQ(values.size(), 1U);
            if (category == in::workspace_category::engine) {
                EXPECT_EQ(root.engine_bytes, 20U);
                EXPECT_EQ(root.engine_high_water, 44U);
                EXPECT_EQ(root.reserved_engine_bytes, 8U);
                EXPECT_EQ(observer.live(), 36U);
                EXPECT_EQ(observer.peak(), 60U);
            } else {
                EXPECT_EQ(root.staging_bytes, 20U);
                EXPECT_EQ(root.staging_high_water, 44U);
                EXPECT_EQ(root.reserved_staging_bytes, 8U);
                EXPECT_EQ(observer.live(), 16U);
                EXPECT_EQ(observer.peak(), 16U);
            }
            values.clear();
            EXPECT_EQ(other_root.engine_bytes, 16U);
            auto& limit = category == in::workspace_category::engine
                ? root.maximum_workspace_bytes : root.maximum_staging_bytes;
            limit = 12U + 8U + sizeof(int64_t) - 1U;
            EXPECT_THROW(values.push_back(7), clipper2next::clipper_error);
            EXPECT_TRUE(values.empty());
            ++limit;
            values.push_back(7);
            EXPECT_EQ(values.size(), 1U);
            EXPECT_EQ(values.front(), 7);
        }
    };
    exercise.template operator()<in::workspace_category::engine>();
    exercise.template operator()<in::workspace_category::staging>();

    struct observing_constructor {
        std::size_t reserved;
        explicit observing_constructor(in::execution_resource_ledger& context) noexcept
            : reserved(context.reserved_engine_bytes) {}
    };
    in::execution_resource_ledger root;
    root.maximum_workspace_bytes = 2U * sizeof(observing_constructor);
    in::engine_buffer<observing_constructor> observed;
    observed.reserve(2);
    observed.bind(&root);
    EXPECT_EQ(observed.emplace_back(root).reserved, sizeof(observing_constructor));
    EXPECT_EQ(observed.emplace_back(root).reserved, sizeof(observing_constructor));
    EXPECT_EQ(root.engine_bytes, 2U * sizeof(observing_constructor));
    EXPECT_EQ(root.reserved_engine_bytes, 0U);
    observed.clear();
    EXPECT_EQ(root.engine_bytes, 0U);
}
