#pragma once

#include "path_equivalence.h"

#include "clipper2/clipper.h"
#include "clipper2next/clipper.h"

#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace clipper2next::tests::oracle {

struct poly_tree_node_signature final {
    bool is_hole{};
    Path64 polygon{};
    std::vector<poly_tree_node_signature> children{};
};

[[nodiscard]] inline auto make_poly_tree_signature(const Clipper2Lib::PolyPath64& node)
    -> poly_tree_node_signature {
    poly_tree_node_signature signature;
    signature.is_hole = node.IsHole();
    signature.polygon = canonical_closed_path(to_next_path(node.Polygon()));
    signature.children.reserve(node.Count());
    for (std::size_t index = 0; index < node.Count(); ++index) {
        signature.children.push_back(make_poly_tree_signature(*node.Child(index)));
    }
    return signature;
}

[[nodiscard]] inline auto make_poly_tree_signature(const PolyTree64& tree,
                                                   PolyTree64::node_id node)
    -> poly_tree_node_signature {
    poly_tree_node_signature signature;
    signature.is_hole = tree.is_hole(node);
    signature.polygon = canonical_closed_path(tree.polygon(node));
    signature.children.reserve(tree.count(node));
    for (const auto child : tree.children(node)) {
        signature.children.push_back(make_poly_tree_signature(tree, child));
    }
    return signature;
}

inline auto assert_poly_tree_signatures_equal(const poly_tree_node_signature& expected,
                                              const poly_tree_node_signature& actual,
                                              std::string path = "root") -> void {
    if (expected.is_hole != actual.is_hole) {
        throw std::runtime_error{"poly-tree hole flag mismatch at " + path};
    }
    if (expected.polygon != actual.polygon) {
        throw std::runtime_error{"poly-tree polygon mismatch at " + path};
    }
    if (expected.children.size() != actual.children.size()) {
        throw std::runtime_error{"poly-tree child count mismatch at " + path + ": expected " +
                                 std::to_string(expected.children.size()) + ", actual " +
                                 std::to_string(actual.children.size())};
    }
    for (std::size_t index = 0; index < expected.children.size(); ++index) {
        assert_poly_tree_signatures_equal(expected.children[index],
                                          actual.children[index],
                                          path + "/" + std::to_string(index));
    }
}

inline auto assert_poly_tree_semantically_equal(const Clipper2Lib::PolyTree64& expected,
                                                const PolyTree64& actual) -> void {
    assert_poly_tree_signatures_equal(make_poly_tree_signature(expected),
                                      make_poly_tree_signature(actual, actual.root()));
}

}  // namespace clipper2next::tests::oracle
