#include "clip/engine/private/engine_scanbeam_processor.h"

#include "clip/engine/private/engine_scanbeam_services.h"

namespace clipper2next::internal {

auto engine_scanbeam_processor::execute(engine_scanbeam_services& services,
                                        ClipType clip_type,
                                        FillRule fill_rule,
                                        bool use_polytrees) -> bool {
    return execute_scanbeam(services, clip_type, fill_rule, use_polytrees);
}

}  // namespace clipper2next::internal
