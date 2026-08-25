#include "clipper2next/clip/topology.h"

#include "api/private/borrowed_topology_pipeline.h"

#include <new>
#include <stdexcept>

namespace clipper2next {

auto clip_topology_checked(const borrowed_clip_request64& request, topology_writer64 writer)
    -> clipper_result<topology_write_stats64> {
    try {
        return internal::execute_borrowed_topology(request, writer);
    } catch (const std::bad_alloc&) {
        return make_clipper_error<topology_write_stats64>(clipper_error_code::allocation_failure);
    } catch (const std::length_error&) {
        return make_clipper_error<topology_write_stats64>(clipper_error_code::resource_limit);
    } catch (const clipper_error& error) {
        return make_clipper_error<topology_write_stats64>(error.code());
    } catch (...) {
        return make_clipper_error<topology_write_stats64>(clipper_error_code::internal_error);
    }
}

}  // namespace clipper2next
