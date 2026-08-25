#include "clip/engine/private/engine_scanbeam_processor.h"

#include "clip/engine/private/engine_scanbeam_services.h"

namespace clipper2next::internal {

auto engine_scanbeam_processor::execute(engine_scanbeam_services& services,
                                        ClipType clip_type,
                                        FillRule fill_rule,
                                        bool use_polytrees) -> bool {
    return execute_scanbeam(services, clip_type, fill_rule, use_polytrees);
}

auto engine_scanbeam_processor::execute_scanbeam(engine_scanbeam_services& services,
                                                 ClipType clip_type,
                                                 FillRule fill_rule,
                                                 bool use_polytrees) -> bool {
    auto& state = context_->state();
    state.cliptype_ = clip_type;
    state.fillrule_ = fill_rule;
    state.using_polytree_ = use_polytrees;
    services.reset();
    int64_t y;
    if (clip_type == ClipType::NoClip || !services.pop_scanline(y)) { return true; }

    while (services.succeeded()) {
        services.insert_local_minima_into_ael(y);
        active_edge_node* horizontal = nullptr;
        while (services.pop_horz(horizontal)) { services.do_horizontal(*horizontal); }
        if (!state.horz_seg_list_.empty()) {
            services.convert_horz_segments_to_joins();
            state.horz_seg_list_.clear();
        }
        state.bot_y_ = y;
        if (!services.pop_scanline(y)) { break; }
        services.do_intersections(y);
        services.do_top_of_scanbeam(y);
        while (services.pop_horz(horizontal)) { services.do_horizontal(*horizontal); }
    }
    if (services.succeeded()) { services.process_horz_joins(); }
    return services.succeeded();
}

}  // namespace clipper2next::internal
