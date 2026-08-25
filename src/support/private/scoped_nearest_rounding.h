#pragma once

#include <cfenv>

namespace clipper2next::internal {

class scoped_nearest_rounding final {
public:
    scoped_nearest_rounding() noexcept
        : previous_mode_{std::fegetround()} {
        if (previous_mode_ != -1 && previous_mode_ != FE_TONEAREST) {
            restore_ = std::fesetround(FE_TONEAREST) == 0;
        }
    }

    scoped_nearest_rounding(const scoped_nearest_rounding&) = delete;
    auto operator=(const scoped_nearest_rounding&)
        -> scoped_nearest_rounding& = delete;

    ~scoped_nearest_rounding() {
        if (restore_) {
            static_cast<void>(std::fesetround(previous_mode_));
        }
    }

private:
    int previous_mode_{};
    bool restore_{};
};

}  // namespace clipper2next::internal
