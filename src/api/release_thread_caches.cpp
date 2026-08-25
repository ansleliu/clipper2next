#include "clipper2next/api/memory.h"

#include "clip/private/clip_execution_strategy.h"
#include "clip/private/borrowed_topology_access.h"
#include "offset/private/offset_thread_state.h"
#include "rectclip/private/rectclip_thread_state.h"
#include "triangulation/private/triangulation_executor.h"

namespace clipper2next {

auto release_thread_caches() noexcept -> void {
    internal::release_clip_thread_state();
    internal::release_borrowed_topology_thread_state();
    internal::release_offset_thread_state();
    internal::release_rectclip_thread_state();
    internal::release_triangulation_thread_state();
}

}  // namespace clipper2next
