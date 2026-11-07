#pragma once
#include "support/private/storage/release_vector.h"
#include "support/private/execution_resource_ledger.h"
#include "support/private/checked_size.h"

#include <array>
#include <utility>
#include <type_traits>

#include "clipper2next/core.h"
#include "offset/private/offset_geometry.h"

namespace clipper2next::internal {

struct offset_state final {
    scoped_workspace_charge normals_charge{};
    scoped_workspace_charge path_out_charge{};
    scoped_workspace_charge callback_charge{};
    scoped_workspace_charge* output_charge{};

    auto bind(execution_resource_ledger* resources, scoped_workspace_charge* output) -> void {
        normals_charge = scoped_workspace_charge{resources};
        path_out_charge = scoped_workspace_charge{resources};
        callback_charge = scoped_workspace_charge{resources};
        output_charge = output;
    }
    auto detach() noexcept -> void {
        normals_charge = scoped_workspace_charge{};
        path_out_charge = scoped_workspace_charge{};
        callback_charge = scoped_workspace_charge{};
        output_charge = nullptr;
    }
    [[nodiscard]] auto reserve_output(std::size_t points, std::size_t descriptor_bytes) -> scoped_workspace_growth {
        const auto previous = output_charge ? output_charge->bytes() : 0U;
        return scoped_workspace_growth{output_charge, checked_size_add(previous,
            checked_size_add(checked_size_multiply(points, sizeof(Point64)), descriptor_bytes)), 1U};
    }
    double group_delta{0.0};
    double temp_limit{0.0};
    offset_arc_parameters arc{};
    PathD normals{};
    Path64 path_out{};
    Path64 callback_path{};

    [[nodiscard]] auto publish_growth(delta_callback_ref callback) noexcept -> scoped_workspace_publication {
        auto* resources = path_out_charge.context();
        return scoped_workspace_publication{
            resources && !callback ? resources->parallel_observer : nullptr};
    }

    auto construct_normals(std::span<const Point64> path) -> void {
        static_assert(std::is_nothrow_default_constructible_v<PointD> &&
                      std::is_nothrow_copy_assignable_v<PointD>);
        assert(normals.empty() && normals_charge.bytes() == 0U);
        if (path.size() <= normals.capacity()) {
            const auto error = normals_charge.try_construct_bytes<workspace_category::engine>(
                checked_size_multiply(path.size(), sizeof(PointD)), [&]() noexcept {
                    normals.resize(path.size());
                });
            if (error != clipper_error_code::ok) { raise_clipper_error(error); }
        } else {
            auto growth = scoped_workspace_growth{normals_charge, path.size(), sizeof(PointD)};
            normals.reserve(path.size());
            normals.resize(path.size());
            growth.commit();
        }
        // Real objects are published before floating-point value calculation.
        internal::fill_normals(normals, path);
    }

    auto clear_scratch() noexcept -> void {
        scoped_workspace_charge::release_storage(
            std::array{&normals_charge, &path_out_charge, &callback_charge}, [this]() noexcept {
                normals.clear();
                path_out.clear();
                callback_path.clear();
            });
    }
    auto reset() noexcept -> void {
        group_delta = 0.0;
        temp_limit = 0.0;
        arc = {};
        clear_scratch();
    }

    auto release() noexcept -> void {
        reset();
        release_vector(normals);
        release_vector(path_out);
        release_vector(callback_path);
    }
};

class offset_resource_scope final {
public:
    offset_resource_scope(offset_state& state, execution_resource_ledger* resources,
                          scoped_workspace_charge* output) : state_(&state) { state_->bind(resources, output); }
    offset_resource_scope(const offset_resource_scope&) = delete;
    auto operator=(const offset_resource_scope&) -> offset_resource_scope& = delete;
    ~offset_resource_scope() { release(); }

    // End the scratch binding when generation finishes, while the enclosing
    // operation lease continues to protect reentry and deferred cache release.
    auto release() noexcept -> void {
        if (auto* state = std::exchange(state_, nullptr)) {
            state->reset();
            state->detach();
        }
    }
private:
    offset_state* state_;
};

}  // namespace clipper2next::internal
