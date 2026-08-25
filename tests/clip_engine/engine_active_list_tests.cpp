#include "clip/engine/private/engine_active_list.h"

#include <gtest/gtest.h>

namespace next = clipper2next;

TEST(Clipper2NextEngineActiveListTests, ActiveListInsertBeforePreservesNeighbors) {
    next::internal::active_edge_node first;
    next::internal::active_edge_node inserted;
    next::internal::active_edge_node before;
    next::internal::active_edge_node after;
    next::internal::active_edge_node* head = &first;

    first.next_in_ael = &before;
    before.prev_in_ael = &first;
    before.next_in_ael = &after;
    after.prev_in_ael = &before;

    next::internal::insert_before_in_ael(inserted, before, head);

    EXPECT_EQ(head, &first);
    EXPECT_EQ(first.next_in_ael.get(), &inserted);
    EXPECT_EQ(inserted.prev_in_ael.get(), &first);
    EXPECT_EQ(inserted.next_in_ael.get(), &before);
    EXPECT_EQ(before.prev_in_ael.get(), &inserted);
    EXPECT_EQ(before.next_in_ael.get(), &after);
    EXPECT_EQ(after.prev_in_ael.get(), &before);
}

TEST(Clipper2NextEngineActiveListTests, ActiveListUnlinkReconnectsNeighbors) {
    next::internal::active_edge_node first;
    next::internal::active_edge_node middle;
    next::internal::active_edge_node last;
    next::internal::active_edge_node* head = &first;

    first.next_in_ael = &middle;
    middle.prev_in_ael = &first;
    middle.next_in_ael = &last;
    last.prev_in_ael = &middle;

    next::internal::unlink_from_ael(middle, head);

    EXPECT_EQ(head, &first);
    EXPECT_EQ(first.next_in_ael.get(), &last);
    EXPECT_EQ(last.prev_in_ael.get(), &first);
    EXPECT_EQ(middle.prev_in_ael.get(), nullptr);
    EXPECT_EQ(middle.next_in_ael.get(), nullptr);
}

TEST(Clipper2NextEngineActiveListTests, ActiveListRemovalUnlinksWithoutOwningLifetime) {
    next::internal::active_edge_node first;
    next::internal::active_edge_node middle;
    next::internal::active_edge_node last;
    next::internal::active_edge_node* head = &first;

    first.next_in_ael = &middle;
    middle.prev_in_ael = &first;
    middle.next_in_ael = &last;
    last.prev_in_ael = &middle;

    EXPECT_TRUE(next::internal::remove_from_ael(middle, head));

    EXPECT_EQ(head, &first);
    EXPECT_EQ(first.next_in_ael.get(), &last);
    EXPECT_EQ(last.prev_in_ael.get(), &first);
    EXPECT_EQ(middle.prev_in_ael.get(), nullptr);
    EXPECT_EQ(middle.next_in_ael.get(), nullptr);
}

TEST(Clipper2NextEngineActiveListTests, ActiveListSwapPositionsUpdatesHeadAndNeighbors) {
    next::internal::active_edge_node first;
    next::internal::active_edge_node second;
    next::internal::active_edge_node third;
    next::internal::active_edge_node* head = &first;

    first.next_in_ael = &second;
    second.prev_in_ael = &first;
    second.next_in_ael = &third;
    third.prev_in_ael = &second;

    next::internal::swap_positions_in_ael(first, second, head);

    EXPECT_EQ(head, &second);
    EXPECT_EQ(second.prev_in_ael.get(), nullptr);
    EXPECT_EQ(second.next_in_ael.get(), &first);
    EXPECT_EQ(first.prev_in_ael.get(), &second);
    EXPECT_EQ(first.next_in_ael.get(), &third);
    EXPECT_EQ(third.prev_in_ael.get(), &first);
}

TEST(Clipper2NextEngineActiveListTests, ActiveListOrderSortsByCurrentX) {
    next::internal::active_edge_node resident;
    next::internal::active_edge_node newcomer;
    resident.current_x = 10;
    newcomer.current_x = 20;

    EXPECT_TRUE(next::internal::is_valid_ael_order(resident, newcomer));

    newcomer.current_x = 5;
    EXPECT_FALSE(next::internal::is_valid_ael_order(resident, newcomer));
}
