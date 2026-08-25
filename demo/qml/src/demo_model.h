#pragma once

#include "clipper2next/clipper.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace clipper2next::demo {

enum class demo_scene {
    boolean_clip,
    offset,
    rect_clip,
    triangulation,
    batch_clip,
};

enum class demo_operation {
    intersection,
    union_op,
    difference,
    xor_op,
};

enum class demo_rect_clip_mode {
    direct,
    prepared,
    immutable,
};

struct demo_parameters {
    demo_scene scene{demo_scene::boolean_clip};
    demo_operation operation{demo_operation::union_op};
    demo_rect_clip_mode rect_clip_mode{demo_rect_clip_mode::direct};
    std::uint32_t seed{1};
    std::size_t path_count{8};
    std::size_t vertex_count{24};
    double offset_delta{18.0};
    std::size_t repeats{1};
    bool use_delaunay{true};
};

struct demo_metrics {
    double algorithm_ms{};
    std::size_t repeat_count{};
    std::size_t input_path_count{};
    std::size_t input_point_count{};
    std::size_t output_path_count{};
    std::size_t output_point_count{};
    double output_area{};
    std::string execution_mode;
};

struct demo_result {
    Paths64 subjects;
    Paths64 clips;
    Paths64 closed_result;
    Paths64 open_result;
    Rect64 clip_rect;
    demo_metrics metrics;
    std::string status;
};

[[nodiscard]] auto run_demo(const demo_parameters& parameters) -> demo_result;

}  // namespace clipper2next::demo
