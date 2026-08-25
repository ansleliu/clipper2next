#include "clip/engine/private/engine_execution_context.h"
#include "clip/engine/private/engine_horizontal.h"
#include "clip/engine/private/engine_intersection_processor.h"
#include "clip/engine/private/engine_scanbeam_orchestrator.h"
#include "clip/engine/private/engine_scanbeam_processor.h"
#include "clip/engine/private/engine_scanbeam_services.h"

#include <gtest/gtest.h>

#include <type_traits>

namespace next = clipper2next;

TEST(Clipper2NextEngineScanbeamTests, EngineExecutionContextReferencesState) {
    next::internal::clipper_base_state state;
    next::internal::engine_execution_context context{state};

    EXPECT_EQ(&context.state(), &state);
    EXPECT_EQ(&context.output_owner(), &state.output_owner_);
}

TEST(Clipper2NextEngineScanbeamTests, EngineScanbeamProcessorReferencesContext) {
    next::internal::clipper_base_state state;
    next::internal::engine_execution_context context{state};
    next::internal::engine_scanbeam_processor processor{context};

    EXPECT_EQ(&processor.context(), &context);
}

TEST(Clipper2NextEngineScanbeamTests, ScanbeamProcessorExposesOwnedScanbeamExecutor) {
    using processor_type = next::internal::engine_scanbeam_processor;
    using services_type = next::internal::engine_scanbeam_services;
    using expected_signature =
        bool (processor_type::*)(services_type&, next::ClipType, next::FillRule, bool);

    EXPECT_TRUE(std::is_member_function_pointer_v<decltype(&processor_type::execute_scanbeam)>);
    EXPECT_TRUE((std::is_same_v<decltype(&processor_type::execute_scanbeam), expected_signature>));
}

TEST(Clipper2NextEngineScanbeamTests, ScanbeamOrchestratorQueuesPromotedHorizontal) {
    next::internal::clipper_base_state state;
    next::internal::Vertex minima_vertex{{0, 10}};
    next::internal::Vertex middle_vertex{{5, 5}};
    next::internal::Vertex horizontal_top{{10, 5}};
    next::internal::Vertex next_vertex{{10, 0}};
    middle_vertex.next = &horizontal_top;
    horizontal_top.prev = &middle_vertex;
    horizontal_top.next = &next_vertex;
    next::internal::local_minimum_node minima{minima_vertex, next::PathType::Subject, false};
    next::internal::active_edge_node edge;
    edge.local_min = &minima;
    edge.bottom = {0, 10};
    edge.top_point = middle_vertex.pt;
    edge.vertex_top = &middle_vertex;
    edge.wind_dx = 1;
    state.actives_ = &edge;
    next::internal::engine_scanbeam_orchestration_options options;
    options.preserve_collinear = true;
    options.has_open_paths = false;
    bool succeeded = true;

    next::internal::do_top_of_scanbeam(state, 5, options, succeeded);

    EXPECT_TRUE(succeeded);
    EXPECT_EQ(edge.bottom, middle_vertex.pt);
    EXPECT_EQ(edge.vertex_top.get(), &horizontal_top);
    EXPECT_TRUE(next::internal::is_horizontal(edge));
    EXPECT_EQ(state.sel_, &edge);
}

TEST(Clipper2NextEngineScanbeamTests, ScanbeamServicesDependOnInternalState) {
    using services_type = next::internal::engine_scanbeam_services;
    using pop_result_type = decltype(std::declval<services_type&>().pop_horz(
        std::declval<next::internal::active_edge_node*&>()));

    static_assert(std::is_constructible_v<services_type,
                                          next::internal::clipper_base_state&,
                                          next::internal::engine_scanbeam_orchestration_options,
                                          bool&,
                                          next::internal::engine_intersection_services&>);
    static_assert(std::is_same_v<pop_result_type, bool>);
}
