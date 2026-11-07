#pragma once

#include <cstddef>

namespace clipper2next::tests {
// Executable-local link seam; no test state is compiled into the library.
auto force_engine_failure(bool fail) noexcept -> void;
[[nodiscard]] auto engine_execution_count() noexcept -> std::size_t;
}  // namespace clipper2next::tests
