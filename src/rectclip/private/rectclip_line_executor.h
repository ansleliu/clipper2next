#pragma once

#include "clipper2next/core.h"

namespace clipper2next {
namespace internal {
class rectclip_line_executor {
public:
    explicit rectclip_line_executor(const Rect64& rect) noexcept;
    auto execute_path(Paths64& output, const Path64& path) -> void;

private:
    const Rect64* rect_;
};
}  // namespace internal
}  // namespace clipper2next
