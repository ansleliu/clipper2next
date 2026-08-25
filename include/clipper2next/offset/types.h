#pragma once

#include <functional>

#include "clipper2next/core.h"

namespace clipper2next {

enum class JoinType { Square, Bevel, Round, Miter };
// Square: joins are squared at exactly the offset distance.
// Bevel: similar to Square, but the offset distance varies with angle.

enum class EndType { Polygon, Joined, Butt, Square, Round };
// Butt: offsets both sides of a path, with square blunt ends.
// Square: offsets both sides of a path, with square extended ends.
// Round: offsets both sides of a path, with round extended ends.
// Joined: offsets both sides of a path, with joined ends.
// Polygon: offsets only one side of a closed path.

using DeltaCallback64 = std::function<double(
    const Path64& path, const PathD& path_normals, size_t curr_idx, size_t prev_idx)>;

class delta_callback_ref final {
public:
    delta_callback_ref() = default;
    constexpr delta_callback_ref(std::nullptr_t) noexcept {}

    explicit delta_callback_ref(const DeltaCallback64& callback) noexcept
        : callback_(&callback) {}

    [[nodiscard]] explicit operator bool() const noexcept {
        return callback_ != nullptr && static_cast<bool>(*callback_);
    }

    [[nodiscard]] auto operator()(const Path64& path,
                                  const PathD& path_normals,
                                  size_t current_index,
                                  size_t previous_index) const -> double {
        return (*callback_)(path, path_normals, current_index, previous_index);
    }

    [[nodiscard]] friend auto operator==(delta_callback_ref callback, std::nullptr_t) noexcept
        -> bool {
        return !callback;
    }

    [[nodiscard]] friend auto operator==(std::nullptr_t, delta_callback_ref callback) noexcept
        -> bool {
        return !callback;
    }

private:
    const DeltaCallback64* callback_ = nullptr;
};

}  // namespace clipper2next
