#pragma once

#include "clipper2next/api/error.h"
#include "clipper2next/api/execution.h"

#include <atomic>
#include <cstddef>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

namespace clipper2next::internal {

class bulk_failure_state final {
public:
    template <typename Operation>
    void invoke(Operation&& operation) noexcept {
        try {
            std::forward<Operation>(operation)();
        } catch (const std::bad_alloc&) {
            record(clipper_error_code::allocation_failure);
        } catch (const std::length_error&) {
            record(clipper_error_code::resource_limit);
        } catch (const clipper_error& error) {
            record(error.code());
        } catch (...) {
            record(clipper_error_code::internal_error);
        }
    }

    [[nodiscard]] auto error() const noexcept -> clipper_error_code {
        const auto value = error_.load(std::memory_order_relaxed);
        return value == no_error
            ? clipper_error_code::ok
            : static_cast<clipper_error_code>(value);
    }

private:
    static constexpr auto no_error =
        (std::numeric_limits<unsigned>::max)();

    void record(const clipper_error_code error) noexcept {
        auto current = error_.load(std::memory_order_relaxed);
        const auto candidate = static_cast<unsigned>(error);
        while (candidate < current &&
               !error_.compare_exchange_weak(
                   current, candidate,
                   std::memory_order_relaxed,
                   std::memory_order_relaxed)) {}
    }

    std::atomic<unsigned> error_{no_error};
};

[[nodiscard]] constexpr auto executor_error_code(
    const bulk_execution_error error) noexcept -> clipper_error_code {
    switch (error) {
    case bulk_execution_error::none:
        return clipper_error_code::ok;
    case bulk_execution_error::allocation_failure:
        return clipper_error_code::allocation_failure;
    case bulk_execution_error::scheduler_failure:
        return clipper_error_code::executor_failure;
    }
    return clipper_error_code::executor_failure;
}

} // namespace clipper2next::internal
