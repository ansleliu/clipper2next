#include "clip/engine/private/engine_types.h"

#include <gtest/gtest.h>

#include <functional>
#include <type_traits>
#include <utility>

namespace next = clipper2next;

TEST(Clipper2NextEngineTypesTests, LocalMinimaStoresVertexByReference) {
    using vertex_member_type = decltype(std::declval<next::internal::local_minimum_node&>().vertex);

    static_assert(
        std::is_same_v<vertex_member_type, std::reference_wrapper<next::internal::Vertex>>);
}

TEST(Clipper2NextEngineTypesTests, CoreEngineRecordsDoNotExposeRawTopologyPointerFields) {
    static_assert(!std::is_pointer_v<decltype(std::declval<next::internal::Vertex&>().next)>);
    static_assert(!std::is_pointer_v<decltype(std::declval<next::internal::Vertex&>().prev)>);
    static_assert(
        !std::is_pointer_v<decltype(std::declval<next::internal::output_point_node&>().next)>);
    static_assert(
        !std::is_pointer_v<decltype(std::declval<next::internal::output_point_node&>().prev)>);
    static_assert(
        !std::is_pointer_v<decltype(std::declval<next::internal::output_point_node&>().outrec)>);
    static_assert(
        !std::is_pointer_v<decltype(std::declval<next::internal::output_record_node&>().owner)>);
    static_assert(!std::is_pointer_v<
                  decltype(std::declval<next::internal::output_record_node&>().front_edge)>);
    static_assert(!std::is_pointer_v<
                  decltype(std::declval<next::internal::output_record_node&>().back_edge)>);
    static_assert(
        !std::is_pointer_v<decltype(std::declval<next::internal::output_record_node&>().pts)>);
    static_assert(
        !std::is_pointer_v<decltype(std::declval<next::internal::active_edge_node&>().outrec)>);
    static_assert(!std::is_pointer_v<
                  decltype(std::declval<next::internal::active_edge_node&>().prev_in_ael)>);
    static_assert(!std::is_pointer_v<
                  decltype(std::declval<next::internal::active_edge_node&>().next_in_ael)>);
    static_assert(!std::is_pointer_v<
                  decltype(std::declval<next::internal::active_edge_node&>().prev_in_sel)>);
    static_assert(!std::is_pointer_v<
                  decltype(std::declval<next::internal::active_edge_node&>().next_in_sel)>);
    static_assert(
        !std::is_pointer_v<decltype(std::declval<next::internal::active_edge_node&>().jump)>);
    static_assert(
        !std::is_pointer_v<decltype(std::declval<next::internal::active_edge_node&>().vertex_top)>);
    static_assert(
        !std::is_pointer_v<decltype(std::declval<next::internal::active_edge_node&>().local_min)>);
}
