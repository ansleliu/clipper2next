#pragma once

#include "clipper2next/api/error.h"

#include <new>
#include <stdexcept>
#include <utility>

namespace clipper2next::internal {

// Only exception classification belongs here. Validation and already-classified
// adapter errors remain with the operation that owns their semantics.
template <typename T, typename Operation>
[[nodiscard]] auto checked_call(Operation&& operation) -> clipper_result<T> {
    try {
        return std::forward<Operation>(operation)();
    } catch (const std::bad_alloc&) {
        return make_clipper_error<T>(clipper_error_code::allocation_failure);
    } catch (const std::length_error&) {
        return make_clipper_error<T>(clipper_error_code::resource_limit);
    } catch (const clipper_error& error) {
        return make_clipper_error<T>(error.code());
    } catch (...) { return make_clipper_error<T>(clipper_error_code::internal_error); }
}

}  // namespace clipper2next::internal
