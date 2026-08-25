#include "clip/engine/private/engine_lifecycle.h"
#include "support/private/checked_size.h"

namespace clipper2next::internal {

auto add_local_minimum(LocalMinimaList& minima, Vertex& vertex, PathType polytype, bool is_open)
    -> void {
    if ((VertexFlags::LocalMin & vertex.flags) != VertexFlags::Empty) { return; }

    vertex.flags = vertex.flags | VertexFlags::LocalMin;
    minima.emplace_back(vertex, polytype, is_open);
}

auto initialize_vertex_path(Vertex* first_vertex,
                            std::size_t vertex_count,
                            PathType polytype,
                            bool is_open,
                            LocalMinimaList& minima) -> void {
    if (!first_vertex || vertex_count == 0U) { return; }

    auto* const last_vertex = first_vertex + vertex_count - 1U;
    for (std::size_t index = 0; index < vertex_count; ++index) {
        auto& current = first_vertex[index];
        current.prev = index == 0U ? last_vertex : &first_vertex[index - 1U];
        current.next = index + 1U == vertex_count ? first_vertex : &first_vertex[index + 1U];
        current.flags = VertexFlags::Empty;
    }
    if (vertex_count < 2U || (vertex_count == 2U && !is_open)) { return; }

    bool going_up;
    bool going_up_initial;
    auto* previous_vertex = first_vertex;
    auto* current_vertex = first_vertex->next.get();
    if (is_open) {
        while (current_vertex != first_vertex && current_vertex->pt.y == first_vertex->pt.y) {
            current_vertex = current_vertex->next.get();
        }
        going_up = current_vertex->pt.y <= first_vertex->pt.y;
        if (going_up) {
            first_vertex->flags = VertexFlags::OpenStart;
            add_local_minimum(minima, *first_vertex, polytype, true);
        } else {
            first_vertex->flags = VertexFlags::OpenStart | VertexFlags::LocalMax;
        }
    } else {
        previous_vertex = first_vertex->prev.get();
        while (previous_vertex != first_vertex && previous_vertex->pt.y == first_vertex->pt.y) {
            previous_vertex = previous_vertex->prev.get();
        }
        if (previous_vertex == first_vertex) { return; }
        going_up = previous_vertex->pt.y > first_vertex->pt.y;
    }

    going_up_initial = going_up;
    previous_vertex = first_vertex;
    current_vertex = first_vertex->next.get();
    while (current_vertex != first_vertex) {
        if (current_vertex->pt.y > previous_vertex->pt.y && going_up) {
            previous_vertex->flags = previous_vertex->flags | VertexFlags::LocalMax;
            going_up = false;
        } else if (current_vertex->pt.y < previous_vertex->pt.y && !going_up) {
            going_up = true;
            add_local_minimum(minima, *previous_vertex, polytype, is_open);
        }
        previous_vertex = current_vertex;
        current_vertex = current_vertex->next.get();
    }

    if (is_open) {
        previous_vertex->flags = previous_vertex->flags | VertexFlags::OpenEnd;
        if (going_up) {
            previous_vertex->flags = previous_vertex->flags | VertexFlags::LocalMax;
        } else {
            add_local_minimum(minima, *previous_vertex, polytype, true);
        }
    } else if (going_up != going_up_initial) {
        if (going_up_initial) {
            add_local_minimum(minima, *previous_vertex, polytype, false);
        } else {
            previous_vertex->flags = previous_vertex->flags | VertexFlags::LocalMax;
        }
    }
}

namespace {

auto add_paths_to_storage(const Paths64& paths,
                          PathType polytype,
                          bool is_open,
                          VertexStorageList& vertex_lists,
                          LocalMinimaList& minima) -> void {
    std::size_t total_vertex_count = 0;
    for (const auto& path : paths) {
        total_vertex_count = checked_size_add(total_vertex_count, path.size());
    }
    if (total_vertex_count == 0) { return; }

    vertex_lists.reserve(checked_size_add(vertex_lists.size(), 1U));
    minima.reserve(
        checked_size_add(minima.size(), checked_size_multiply(paths.size(), 2U)));
    Vertex* vertex_storage = vertex_lists.acquire(total_vertex_count);
    Vertex* vertex = vertex_storage;
    for (const Path64& path : paths) {
        Vertex* first_vertex = vertex;
        if (path.empty()) { continue; }

        bool has_previous = false;
        Point64 previous_point{};
        for (const Point64& point : path) {
            if (has_previous && point == previous_point) { continue; }
            vertex->pt = point;
            previous_point = point;
            has_previous = true;
            ++vertex;
        }
        auto vertex_count = static_cast<std::size_t>(vertex - first_vertex);
        if (!is_open && vertex_count > 1U && first_vertex->pt == (vertex - 1U)->pt) {
            --vertex;
            --vertex_count;
        }
        initialize_vertex_path(first_vertex, vertex_count, polytype, is_open, minima);
    }
}

}  // namespace

auto add_paths_to_state(reuseable_data_state& state,
                        const Paths64& paths,
                        PathType polytype,
                        bool is_open) -> void {
    add_paths_to_storage(paths, polytype, is_open, state.vertex_lists_, state.minima_list_);
}

auto add_paths_to_state(clipper_base_state& state,
                        const Paths64& paths,
                        PathType polytype,
                        bool is_open) -> void {
    state.minima_list_sorted_ = false;
    add_paths_to_storage(paths, polytype, is_open, state.vertex_lists_, state.minima_list_);
}

auto append_reuseable_data(clipper_base_state& state,
                           const reuseable_data_state& reuseable_data,
                           bool& has_open_paths) -> void {
    state.minima_list_sorted_ = false;
    state.minima_list_.reserve(
        checked_size_add(state.minima_list_.size(), reuseable_data.minima_list_.size()));
    state.minima_list_.insert(state.minima_list_.end(),
                              reuseable_data.minima_list_.begin(),
                              reuseable_data.minima_list_.end());

    if (!has_open_paths) {
        for (const auto& local_minimum : reuseable_data.minima_list_) {
            if (local_minimum.is_open) {
                has_open_paths = true;
                break;
            }
        }
    }
}

}  // namespace clipper2next::internal
