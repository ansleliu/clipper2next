#pragma once

#include "clipper2next/api/export.h"

namespace clipper2next {

// Frees the thread-local working state and result caches (clip engine scratch,
// rectclip bounds buffer, triangulation result cache) retained by the calling
// thread. Long-lived worker threads in thread pools can call this after a
// burst of geometry work to return that memory to the allocator. Subsequent
// calls on the thread simply re-grow the state on demand.
CLIPPER2NEXT_API auto release_thread_caches() noexcept -> void;

}  // namespace clipper2next
