#pragma once

#include <vector>
namespace clipper2next::internal {

// Empty shrink returns storage without constructing another container (and its
// checked-iterator proxy). This no-allocation path is gated on supported STLs.
template <class T, class Allocator>
auto release_vector(std::vector<T, Allocator>& values) noexcept -> void {
    values.clear();
    values.shrink_to_fit();
}

}  // namespace clipper2next::internal
