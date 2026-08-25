#include <gtest/gtest.h>

#include "clipper2next/api/options.h"
#include "clipper2next/clipper.h"
#include "offset/private/offset_execution_context.h"
#include "offset/private/offset_path_processor.h"
#include "support/test_paths.h"

#include <type_traits>
#include <utility>

namespace next = clipper2next;
namespace test = clipper2next::tests;

TEST(Clipper2NextOffsetExecutionTests, HotPathUsesNonOwningDeltaCallbackView) {
    using callback_view = next::delta_callback_ref;
    using context = next::internal::offset_execution_context;

    static_assert(std::is_invocable_r_v<double,
                                        callback_view,
                                        const next::Path64&,
                                        const next::PathD&,
                                        std::size_t,
                                        std::size_t>);
    static_assert(
        std::is_same_v<decltype(std::declval<const context&>().delta_callback()), callback_view>);
}

TEST(Clipper2NextOffsetExecutionTests, OffsetExecutionContextStoresOutputTargets) {
    next::internal::offset_state state;
    next::Paths64 solution;
    next::execution_options options;

    next::internal::offset_execution_context context{state, solution, nullptr, options, nullptr};

    EXPECT_EQ(&context.state(), &state);
    EXPECT_EQ(&context.paths_solution(), &solution);
    EXPECT_EQ(context.poly_tree_solution(), nullptr);
    EXPECT_EQ(context.delta_callback(), nullptr);
}

TEST(Clipper2NextOffsetExecutionTests, OffsetPathProcessorReferencesExecutionContext) {
    next::internal::offset_state state;
    next::Paths64 solution;
    next::execution_options options;
    next::internal::offset_execution_context context{state, solution, nullptr, options, nullptr};
    next::internal::offset_path_processor processor{context};

    EXPECT_EQ(&processor.context(), &context);
}

TEST(Clipper2NextOffsetExecutionTests, OffsetPathProcessorReusesNormalStorage) {
    next::internal::offset_state state;
    state.normals.reserve(64);
    const auto reserved_capacity = state.normals.capacity();
    next::Paths64 solution;
    next::execution_options options;
    next::internal::offset_execution_context context{state, solution, nullptr, options, nullptr};
    next::internal::offset_path_processor processor{context};

    processor.build_normals(test::path64({0, 0, 10, 0, 10, 10, 0, 10}));

    EXPECT_EQ(state.normals.size(), 4U);
    EXPECT_GE(state.normals.capacity(), reserved_capacity);
}
