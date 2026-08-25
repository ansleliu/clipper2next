#include "offset/private/offset_path_processor.h"

#include "offset/private/offset_geometry.h"

namespace clipper2next::internal {

auto offset_path_processor::build_normals(const Path64& path) -> void {
    internal::assign_normals(context_->state().normals, path);
}

}  // namespace clipper2next::internal
