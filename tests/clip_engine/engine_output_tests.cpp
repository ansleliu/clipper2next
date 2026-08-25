#include "clip/engine/private/engine_output.h"
#include "clip/engine/private/engine_output_owner.h"
#include "clip/engine/private/engine_output_topology.h"

#include <gtest/gtest.h>

#include <type_traits>
#include <utility>

namespace next = clipper2next;

TEST(Clipper2NextEngineOutputTests, OutputOwnerCreationApisDoNotReturnRawPointers) {
    using owner_type = next::internal::engine_output_owner;
    using record_type = next::internal::output_record_node;

    static_assert(
        std::is_lvalue_reference_v<decltype(std::declval<owner_type&>().create_outrec())>);
    static_assert(!std::is_pointer_v<decltype(std::declval<owner_type&>().create_outrec())>);
    static_assert(std::is_lvalue_reference_v<decltype(std::declval<owner_type&>().create_outpt(
                      std::declval<const next::Point64&>(), std::declval<record_type&>()))>);
    static_assert(!std::is_pointer_v<decltype(std::declval<owner_type&>().create_outpt(
                      std::declval<const next::Point64&>(), std::declval<record_type&>()))>);
}

TEST(Clipper2NextEngineOutputTests, OutPointDoesNotStoreHorizontalSegmentPointer) {
    static_assert(std::is_same_v<decltype(std::declval<next::internal::output_point_node&>()
                                              .has_horizontal_segment),
                                 bool>);
}

TEST(Clipper2NextEngineOutputTests, OutPointRingCountsDistinctNodes) {
    next::internal::engine_output_owner owner;
    auto* output_record = &owner.create_outrec();
    auto* first = &owner.create_outpt({0, 0}, *output_record);
    output_record->pts = first;
    auto* second = next::internal::duplicate_out_point(first, true);
    second->pt = {10, 0};

    EXPECT_EQ(next::internal::point_count(first), 2);

    next::internal::dispose_out_points(output_record);
}

TEST(Clipper2NextEngineOutputTests, OutputOwnerDisposesOutRecAndClearsSplits) {
    next::internal::engine_output_owner owner;
    auto* output_record = &owner.create_outrec();
    output_record->splits.emplace_back(output_record);

    owner.dispose_all();

    EXPECT_TRUE(owner.records().empty());
}

TEST(Clipper2NextEngineOutputTests, OutputOwnerCreatesIndexedOutRecs) {
    next::internal::engine_output_owner owner;

    auto* first = &owner.create_outrec();
    auto* second = &owner.create_outrec();

    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(first->idx, 0U);
    EXPECT_EQ(second->idx, 1U);
}

TEST(Clipper2NextEngineOutputTests, OutputTopologyLinksInsertedPoint) {
    next::internal::engine_output_owner owner;
    auto* output_record = &owner.create_outrec();
    auto* first = &owner.create_outpt({0, 0}, *output_record);
    auto* inserted = next::internal::insert_output_point_after(first, {10, 0});

    ASSERT_NE(inserted, nullptr);
    EXPECT_EQ(first->next.get(), inserted);
    EXPECT_EQ(inserted->prev.get(), first);
    EXPECT_EQ(inserted->outrec.get(), output_record);

    output_record->pts = first;
    next::internal::dispose_out_points(output_record);
}
