#pragma once

#include "clip/engine/private/engine_state.h"

namespace clipper2next::internal {

auto add_local_minimum(LocalMinimaList& minima, Vertex& vertex, PathType polytype, bool is_open)
    -> void;

auto initialize_vertex_path(Vertex* first_vertex,
                            std::size_t vertex_count,
                            PathType polytype,
                            bool is_open,
                            LocalMinimaList& minima) -> void;

auto add_paths_to_state(reuseable_data_state& state,
                        const Paths64& paths,
                        PathType polytype,
                        bool is_open) -> void;

auto add_paths_to_state(clipper_base_state& state,
                        const Paths64& paths,
                        PathType polytype,
                        bool is_open) -> void;

auto append_reuseable_data(clipper_base_state& state,
                           const reuseable_data_state& reuseable_data,
                           bool& has_open_paths) -> void;

auto dispose_active_edges(active_edge_node*& head) noexcept -> void;
auto dispose_active_edges(active_edge_node_ref& head) noexcept -> void;
auto dispose_vertices_and_local_minima(reuseable_data_state& state) noexcept -> void;
auto dispose_vertices_and_local_minima(clipper_base_state& state) noexcept -> void;
auto cleanup_engine_state(clipper_base_state& state) noexcept -> void;
auto reset_engine_state_for_reuse(clipper_base_state& state) noexcept -> void;
auto release_engine_state_storage(clipper_base_state& state) noexcept -> void;
auto reset_engine_state(clipper_base_state& state) -> void;

}  // namespace clipper2next::internal
