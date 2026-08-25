// SPDX-License-Identifier: BSL-1.0

#pragma once

#include <expected>
#include <stdexcept>

namespace clipper2next {

enum class clipper_error_code {
    ok,
    precision_out_of_range,
    scale_out_of_range,
    non_pair_input,
    coordinate_range,
    resource_limit,
    allocation_failure,
    input_access_failure,
    input_changed,
    executor_failure,
    sink_failure,
    internal_error,
};

[[nodiscard]] inline constexpr auto clipper_error_message(clipper_error_code code) noexcept -> const
    char* {
    switch (code) {
    case clipper_error_code::ok: {
        return "OK";
    }
    case clipper_error_code::precision_out_of_range: {
        return "Precision exceeds the permitted range";
    }
    case clipper_error_code::scale_out_of_range: {
        return "Invalid scale (either 0 or too large)";
    }
    case clipper_error_code::non_pair_input: {
        return "There must be 2 values for each coordinate";
    }
    case clipper_error_code::coordinate_range: {
        return "Values exceed permitted range";
    }
    case clipper_error_code::resource_limit: {
        return "A caller supplied resource limit was exceeded";
    }
    case clipper_error_code::allocation_failure: {
        return "Memory allocation failed";
    }
    case clipper_error_code::input_access_failure: {
        return "Borrowed input access failed";
    }
    case clipper_error_code::input_changed: {
        return "Borrowed input changed during execution";
    }
    case clipper_error_code::executor_failure: {
        return "The supplied executor failed to complete synchronous work";
    }
    case clipper_error_code::sink_failure: {
        return "Topology sink rejected output";
    }
    case clipper_error_code::internal_error: {
        return "There is an undefined error in clipper2next";
    }
    }
    return "Unknown error";
}

class clipper_error final : public std::runtime_error {
public:
    explicit clipper_error(clipper_error_code code)
        : std::runtime_error(clipper_error_message(code)),
          code_(code) {}

    [[nodiscard]] auto code() const noexcept -> clipper_error_code { return code_; }

private:
    clipper_error_code code_;
};

template <class T>
using clipper_result = std::expected<T, clipper_error_code>;

template <class T>
[[nodiscard]] inline auto make_clipper_error(clipper_error_code code) -> clipper_result<T> {
    return std::unexpected(code);
}

[[noreturn]] inline void raise_clipper_error(clipper_error_code code) {
#if (defined(__cpp_exceptions) && __cpp_exceptions) || (defined(__EXCEPTIONS) && __EXCEPTIONS)
    throw clipper_error(code);
#else
    (void)code;
    std::terminate();
#endif
}

}  // namespace clipper2next
