#include "rectclip/private/rectclip_edges.h"

#include <cstddef>

namespace clipper2next::internal {
namespace {

[[nodiscard]] auto has_horizontal_overlap(const Point64& left1,
                                          const Point64& right1,
                                          const Point64& left2,
                                          const Point64& right2) -> bool {
    return (left1.x < right2.x) && (right1.x > left2.x);
}

[[nodiscard]] auto has_vertical_overlap(const Point64& top1,
                                        const Point64& bottom1,
                                        const Point64& top2,
                                        const Point64& bottom2) -> bool {
    return (top1.y < bottom2.y) && (bottom1.y > top2.y);
}

[[nodiscard]] auto is_detached_or_collapsed(const rectclip_node& node) -> bool {
    return (node.next == node.prev) || (node.pt == (*node.prev).pt);
}

[[nodiscard]] auto is_previous_segment_larger(const rectclip_node& node, bool is_horizontal)
    -> bool {
    const auto& previous = *node.prev;
    return is_horizontal ? (node.pt.x > previous.pt.x) : (node.pt.y > previous.pt.y);
}

[[nodiscard]] auto is_ring_exhausted(const rectclip_node_ref& node) -> bool {
    return !node || ((*node).next == (*node).prev);
}

[[nodiscard]] auto spans_overlap(bool is_horizontal,
                                 const rectclip_node& p1,
                                 const rectclip_node& p1a,
                                 const rectclip_node& p2,
                                 const rectclip_node& p2a) -> bool {
    if (is_horizontal) { return has_horizontal_overlap(p1.pt, p1a.pt, p2.pt, p2a.pt); }
    return has_vertical_overlap(p1.pt, p1a.pt, p2.pt, p2a.pt);
}

auto reconnect_joined_ring(bool clockwise_is_toward_larger,
                           rectclip_node& p1,
                           rectclip_node& p1a,
                           rectclip_node& p2,
                           rectclip_node& p2a) -> void {
    if (clockwise_is_toward_larger) {
        p1.next = p2;
        p2.prev = p1;
        p1a.prev = p2a;
        p2a.next = p1a;
        return;
    }
    p1.prev = p2;
    p2.next = p1;
    p1a.next = p2a;
    p2a.prev = p1a;
}

auto assign_split_edges(bool clockwise_is_toward_larger,
                        rectclip_node_list& clockwise,
                        rectclip_node_list& counter_clockwise,
                        std::size_t& i,
                        std::size_t& j,
                        rectclip_node* node,
                        rectclip_node* node2,
                        bool node_is_larger,
                        bool node2_is_larger) -> void {
    if (is_detached_or_collapsed(*node)) {
        if (node2_is_larger == clockwise_is_toward_larger) {
            clockwise[i] = node2;
            counter_clockwise[j++] = nullptr;
        } else {
            counter_clockwise[j] = node2;
            clockwise[i++] = nullptr;
        }
        return;
    }

    if (is_detached_or_collapsed(*node2)) {
        if (node_is_larger == clockwise_is_toward_larger) {
            clockwise[i] = node;
            counter_clockwise[j++] = nullptr;
        } else {
            counter_clockwise[j] = node;
            clockwise[i++] = nullptr;
        }
        return;
    }

    if (node_is_larger == node2_is_larger) {
        if (node_is_larger == clockwise_is_toward_larger) {
            clockwise[i] = node;
            uncouple_edge(node2);
            add_to_edge(clockwise, node2);
            counter_clockwise[j++] = nullptr;
        } else {
            clockwise[i++] = nullptr;
            counter_clockwise[j] = node2;
            uncouple_edge(node);
            add_to_edge(counter_clockwise, node);
            j = 0;
        }
        return;
    }

    if (node_is_larger == clockwise_is_toward_larger) {
        clockwise[i] = node;
    } else {
        counter_clockwise[j] = node;
    }
    if (node2_is_larger == clockwise_is_toward_larger) {
        clockwise[i] = node2;
    } else {
        counter_clockwise[j] = node2;
    }
}

}  // namespace

auto tidy_edges(std::size_t index,
                rectclip_node_list& clockwise,
                rectclip_node_list& counter_clockwise,
                rectclip_node_list& results) -> void {
    if (counter_clockwise.empty()) { return; }
    const bool is_horizontal = (index == 1) || (index == 3);
    const bool clockwise_is_toward_larger = (index == 1) || (index == 2);
    std::size_t i = 0;
    std::size_t j = 0;

    while (i < clockwise.size()) {
        if (is_ring_exhausted(clockwise[i])) {
            clockwise[i++] = nullptr;
            j = 0;
            continue;
        }

        const std::size_t j_limit = counter_clockwise.size();
        while (j < j_limit && is_ring_exhausted(counter_clockwise[j])) { ++j; }

        if (j == j_limit) {
            ++i;
            j = 0;
            continue;
        }

        rectclip_node* p1 =
            clockwise_is_toward_larger ? (*clockwise[i]).prev.get() : clockwise[i].get();
        rectclip_node* p1a =
            clockwise_is_toward_larger ? clockwise[i].get() : (*clockwise[i]).prev.get();
        rectclip_node* p2 = clockwise_is_toward_larger ? counter_clockwise[j].get()
                                                       : (*counter_clockwise[j]).prev.get();
        rectclip_node* p2a = clockwise_is_toward_larger ? (*counter_clockwise[j]).prev.get()
                                                        : counter_clockwise[j].get();

        if (!spans_overlap(is_horizontal, *p1, *p1a, *p2, *p2a)) {
            ++j;
            continue;
        }

        const bool is_rejoining =
            (*clockwise[i]).owner_index != (*counter_clockwise[j]).owner_index;

        if (is_rejoining) {
            results[(*p2).owner_index] = nullptr;
            set_new_owner(p2, (*p1).owner_index);
        }

        reconnect_joined_ring(clockwise_is_toward_larger, *p1, *p1a, *p2, *p2a);

        if (!is_rejoining) {
            const std::size_t new_index = results.size();
            results.emplace_back(p1a);
            set_new_owner(p1a, new_index);
        }

        rectclip_node* node = clockwise_is_toward_larger ? p2 : p1;
        rectclip_node* node2 = clockwise_is_toward_larger ? p1a : p2a;
        results[(*node).owner_index] = node;
        results[(*node2).owner_index] = node2;

        const bool node_is_larger = is_previous_segment_larger(*node, is_horizontal);
        const bool node2_is_larger = is_previous_segment_larger(*node2, is_horizontal);
        assign_split_edges(clockwise_is_toward_larger,
                           clockwise,
                           counter_clockwise,
                           i,
                           j,
                           node,
                           node2,
                           node_is_larger,
                           node2_is_larger);
    }
}

}  // namespace clipper2next::internal
