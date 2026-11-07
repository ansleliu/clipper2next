#include "rectclip/private/rectclip_path_builder.h"

namespace clipper2next::internal {
namespace {

[[nodiscard]] auto unlink_node(rectclip_node* node) -> rectclip_node* {
    if (node->next == node) { return nullptr; }
    node->prev->next = node->next;
    node->next->prev = node->prev;
    return node->next;
}

}  // namespace

auto prepare_polygon_path(rectclip_node*& node) -> void {
    if (!node || node->next == node->prev) {
        node = nullptr;
        return;
    }

    rectclip_node* node2 = node->next;
    while (node2 && node2 != node) {
        if (is_collinear(node2->prev->pt, node2->pt, node2->next->pt)) {
            node = node2->prev;
            node2 = unlink_node(node2);
        } else {
            node2 = node2->next;
        }
    }
    node = node2;
}

}  // namespace clipper2next::internal
