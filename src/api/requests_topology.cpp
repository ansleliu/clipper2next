#include "clipper2next/clip/topology.h"
#include "api/private/checked_execution.h"

#include "api/private/borrowed_topology_pipeline.h"

namespace clipper2next {

auto clip_topology_checked(const borrowed_clip_request64& request, topology_writer64 writer)
    -> clipper_result<topology_write_stats64> {
    return internal::checked_call<topology_write_stats64>(
        [&]() -> clipper_result<topology_write_stats64> {
            return internal::execute_borrowed_topology(request, writer);
        });
}

}  // namespace clipper2next
