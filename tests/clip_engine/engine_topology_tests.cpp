#include "clip/engine/private/engine_path_builder.h"
#include "clip/engine/private/engine_topology.h"

#include <gtest/gtest.h>

namespace next = clipper2next;

namespace {

void LinkRing(next::internal::output_point_node& first,
              next::internal::output_point_node& second,
              next::internal::output_point_node& third) {
    first.next = &second;
    first.prev = &third;
    second.next = &third;
    second.prev = &first;
    third.next = &first;
    third.prev = &second;
}

void LinkRing(next::internal::output_point_node& first,
              next::internal::output_point_node& second,
              next::internal::output_point_node& third,
              next::internal::output_point_node& fourth) {
    first.next = &second;
    first.prev = &fourth;
    second.next = &third;
    second.prev = &first;
    third.next = &fourth;
    third.prev = &second;
    fourth.next = &first;
    fourth.prev = &third;
}

}  // namespace

TEST(Clipper2NextEngineTopologyTests, SetOwnerUpdatesOutRecOwner) {
    next::internal::output_record_node child;
    next::internal::output_record_node owner;

    next::internal::set_owner(&child, &owner);

    EXPECT_EQ(child.owner.get(), &owner);
}

TEST(Clipper2NextEngineTopologyTests, GetRealOutRecSkipsRecursiveSplits) {
    next::internal::output_record_node real_owner;
    next::internal::output_point_node owner_point{{0, 0}, real_owner};
    real_owner.pts = &owner_point;

    next::internal::output_record_node split;
    split.owner = &real_owner;

    EXPECT_EQ(next::internal::get_real_outrec(&split), &real_owner);
}

TEST(Clipper2NextEngineTopologyTests, ReverseOutPointsPreservesCircularLinks) {
    next::internal::output_record_node owner;
    next::internal::output_point_node first{{0, 0}, owner};
    next::internal::output_point_node second{{10, 0}, owner};
    next::internal::output_point_node third{{10, 10}, owner};
    LinkRing(first, second, third);

    next::internal::reverse_out_points(&first);

    EXPECT_EQ(first.next.get(), &third);
    EXPECT_EQ(first.prev.get(), &second);
    EXPECT_EQ(second.next.get(), &first);
    EXPECT_EQ(second.prev.get(), &third);
    EXPECT_EQ(third.next.get(), &second);
    EXPECT_EQ(third.prev.get(), &first);
}

TEST(Clipper2NextEngineTopologyTests, BuildPath64RejectsInvalidSinglePointRing) {
    next::internal::output_record_node owner;
    next::internal::output_point_node single{{0, 0}, owner};
    next::Path64 path;

    EXPECT_FALSE(next::internal::build_path64(&single, false, false, path));
}

TEST(Clipper2NextEngineTopologyTests, BuildPath64PreservesRingOrder) {
    next::internal::output_record_node owner;
    next::internal::output_point_node first{{0, 0}, owner};
    next::internal::output_point_node second{{10, 0}, owner};
    next::internal::output_point_node third{{10, 10}, owner};
    LinkRing(first, second, third);

    next::Path64 path;
    ASSERT_TRUE(next::internal::build_path64(&first, false, false, path));

    ASSERT_EQ(path.size(), 3U);
    EXPECT_EQ(path[0], second.pt);
    EXPECT_EQ(path[1], third.pt);
    EXPECT_EQ(path[2], first.pt);
}

TEST(Clipper2NextEngineTopologyTests,
     BoundaryVerticesWithInteriorChordsAreContained) {
    next::internal::output_record_node outer_owner;
    next::internal::output_point_node outer_first{{0, 0}, outer_owner};
    next::internal::output_point_node outer_second{{10, 0}, outer_owner};
    next::internal::output_point_node outer_third{{10, 10}, outer_owner};
    next::internal::output_point_node outer_fourth{{0, 10}, outer_owner};
    LinkRing(outer_first, outer_second, outer_third, outer_fourth);

    next::internal::output_record_node inner_owner;
    next::internal::output_point_node inner_first{{0, 5}, inner_owner};
    next::internal::output_point_node inner_second{{5, 0}, inner_owner};
    next::internal::output_point_node inner_third{{10, 5}, inner_owner};
    next::internal::output_point_node inner_fourth{{5, 10}, inner_owner};
    LinkRing(inner_first, inner_second, inner_third, inner_fourth);

    EXPECT_TRUE(next::internal::path2_contains_path1(
        &inner_first, &outer_first));
}

TEST(Clipper2NextEngineTopologyTests, CoincidentBoundaryRingIsNotNested) {
    next::internal::output_record_node first_owner;
    next::internal::output_point_node first_a{{0, 0}, first_owner};
    next::internal::output_point_node first_b{{10, 0}, first_owner};
    next::internal::output_point_node first_c{{10, 10}, first_owner};
    next::internal::output_point_node first_d{{0, 10}, first_owner};
    LinkRing(first_a, first_b, first_c, first_d);

    next::internal::output_record_node second_owner;
    next::internal::output_point_node second_a{{0, 0}, second_owner};
    next::internal::output_point_node second_b{{10, 0}, second_owner};
    next::internal::output_point_node second_c{{10, 10}, second_owner};
    next::internal::output_point_node second_d{{0, 10}, second_owner};
    LinkRing(second_a, second_b, second_c, second_d);

    EXPECT_FALSE(next::internal::path2_contains_path1(&first_a, &second_a));
}
