#pragma once

#include <vector>

#include "clipper2next/core.h"
#include "support/private/storage/topology_store.h"

namespace clipper2next::internal {

// The enum order is part of the rectangular boundary traversal contract.
enum class rect_location { Left, Top, Right, Bottom, Inside };

struct rectclip_node;
struct rectclip_node_tag;
struct rectclip_node_list_tag;

using rectclip_node_ref = topology_ref<rectclip_node, rectclip_node_tag>;
using rectclip_node_list = std::vector<rectclip_node_ref>;
using rectclip_node_list_ref = topology_ref<rectclip_node_list, rectclip_node_list_tag>;

struct rectclip_node final {
    Point64 pt{};
    std::size_t owner_index{0};
    rectclip_node_list_ref edge{};
    rectclip_node_ref next{};
    rectclip_node_ref prev{};

    rectclip_node() = default;
    explicit rectclip_node(Point64 point) noexcept
        : pt(point) {}
};

}  // namespace clipper2next::internal
