#include "support/engine_execution_override.h"
#include "clip/engine/private/engine_scanbeam_processor.h"

namespace {
bool fail_execution{};
std::size_t execution_count{};
}  // namespace

namespace clipper2next::tests {
auto force_engine_failure(bool fail) noexcept -> void {
    fail_execution = fail;
    execution_count = 0;
}
auto engine_execution_count() noexcept -> std::size_t {
    return execution_count;
}
}  // namespace clipper2next::tests

namespace clipper2next::internal {
auto engine_scanbeam_processor::execute(engine_scanbeam_services& services,
                                        ClipType clip_type,
                                        FillRule fill_rule,
                                        bool use_polytrees) -> bool {
    ++execution_count;
    if (fail_execution) { return false; }
    return execute_scanbeam(services, clip_type, fill_rule, use_polytrees);
}
}  // namespace clipper2next::internal
