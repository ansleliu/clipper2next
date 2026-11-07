#include "clip/engine/private/engine_output_owner.h"
#include "support/private/storage/release_vector.h"

#include "clip/engine/private/engine_output.h"

namespace clipper2next::internal {

engine_output_owner::~engine_output_owner() {
    dispose_all();
}

auto engine_output_owner::dispose_all() noexcept -> void {
    records_.clear();
    point_pool_.clear();
    record_pool_.clear();
}

auto engine_output_owner::release() noexcept -> void {
    release_vector(records_);
    point_pool_.release();
    record_pool_.release();
}

}  // namespace clipper2next::internal
