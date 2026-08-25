#include "demo_model.h"

#include "clipper2next/batch.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace clipper2next::demo {
namespace {

constexpr double pi = 3.141592653589793238462643383279502884;

[[nodiscard]] auto bounded_count(std::size_t value, std::size_t low, std::size_t high)
    -> std::size_t {
    return (std::min)((std::max)(value, low), high);
}

[[nodiscard]] auto count_points(const Paths64& paths) -> std::size_t {
    std::size_t count{};
    for (const auto& path : paths) { count += path.size(); }
    return count;
}

[[nodiscard]] auto to_clip_type(demo_operation operation) -> ClipType {
    switch (operation) {
        case demo_operation::intersection:
            return ClipType::Intersection;
        case demo_operation::union_op:
            return ClipType::Union;
        case demo_operation::difference:
            return ClipType::Difference;
        case demo_operation::xor_op:
            return ClipType::Xor;
    }
    return ClipType::Union;
}

[[nodiscard]] auto rect_clip_mode_label(demo_rect_clip_mode mode) -> std::string {
    switch (mode) {
        case demo_rect_clip_mode::direct:
            return "direct";
        case demo_rect_clip_mode::prepared:
            return "prepared";
        case demo_rect_clip_mode::immutable:
            return "immutable";
    }
    return "direct";
}

[[nodiscard]] auto make_polygon(std::mt19937& rng,
                                int64_t center_x,
                                int64_t center_y,
                                int64_t radius_x,
                                int64_t radius_y,
                                std::size_t vertex_count) -> Path64 {
    const auto vertices = bounded_count(vertex_count, 3U, 256U);
    std::uniform_real_distribution<double> jitter(0.88, 1.12);

    Path64 path;
    path.reserve(vertices);
    for (std::size_t index = 0; index < vertices; ++index) {
        const auto angle = (2.0 * pi * static_cast<double>(index)) / static_cast<double>(vertices);
        const auto x = static_cast<int64_t>(
            std::llround(static_cast<double>(center_x) +
                         std::cos(angle) * static_cast<double>(radius_x) * jitter(rng)));
        const auto y = static_cast<int64_t>(
            std::llround(static_cast<double>(center_y) +
                         std::sin(angle) * static_cast<double>(radius_y) * jitter(rng)));
        path.emplace_back(x, y);
    }
    return path;
}

[[nodiscard]] auto make_polygon_grid(std::uint32_t seed,
                                      std::size_t path_count,
                                      std::size_t vertex_count,
                                      int64_t x_offset,
                                      int64_t y_offset) -> Paths64 {
    const auto count = bounded_count(path_count, 1U, 256U);
    std::mt19937 rng(seed);

    Paths64 paths;
    paths.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        const auto column = static_cast<int64_t>(index % 8U);
        const auto row = static_cast<int64_t>(index / 8U);
        const auto center_x = x_offset + 120 + column * 96;
        const auto center_y = y_offset + 110 + row * 82;
        paths.push_back(make_polygon(rng, center_x, center_y, 42, 34, vertex_count));
    }
    return paths;
}

[[nodiscard]] auto make_boolean_subjects(const demo_parameters& parameters) -> Paths64 {
    return make_polygon_grid(parameters.seed, parameters.path_count, parameters.vertex_count, 0, 0);
}

[[nodiscard]] auto make_boolean_clips(const demo_parameters& parameters) -> Paths64 {
    const auto count = (std::max)(std::size_t{1}, parameters.path_count / 2U);
    return make_polygon_grid(parameters.seed + 7919U, count, parameters.vertex_count, 34, 24);
}

[[nodiscard]] auto make_triangulation_subject(const demo_parameters& parameters) -> Paths64 {
    std::mt19937 rng(parameters.seed + 31337U);
    return Paths64{make_polygon(rng,
                                500,
                                420,
                                280,
                                210,
                                bounded_count(parameters.vertex_count, 3U, 512U))};
}

[[nodiscard]] auto base_metrics(const Paths64& subjects,
                                const Paths64& clips,
                                std::size_t repeats,
                                std::string execution_mode) -> demo_metrics {
    demo_metrics metrics;
    metrics.repeat_count = repeats;
    metrics.input_path_count = subjects.size() + clips.size();
    metrics.input_point_count = count_points(subjects) + count_points(clips);
    metrics.execution_mode = std::move(execution_mode);
    return metrics;
}

auto finish_metrics(demo_metrics& metrics, const Paths64& closed, const Paths64& open) -> void {
    metrics.output_path_count = closed.size() + open.size();
    metrics.output_point_count = count_points(closed) + count_points(open);
    metrics.output_area = area(closed);
}

template <typename Callable>
auto measure_repeated(std::size_t repeats, Callable&& callable) {
    const auto count = (std::max)(std::size_t{1}, repeats);
    const auto start = std::chrono::steady_clock::now();
    auto result = callable();
    for (std::size_t index = 1; index < count; ++index) { result = callable(); }
    const auto stop = std::chrono::steady_clock::now();
    const auto elapsed = std::chrono::duration<double, std::milli>(stop - start).count();
    return std::pair{std::move(result), elapsed / static_cast<double>(count)};
}

[[nodiscard]] auto run_boolean_clip(const demo_parameters& parameters) -> demo_result {
    demo_result result;
    result.subjects = make_boolean_subjects(parameters);
    result.clips = make_boolean_clips(parameters);

    clip_request64 request;
    request.clip_type = to_clip_type(parameters.operation);
    request.fill_rule = FillRule::NonZero;
    request.subjects = result.subjects;
    request.clips = result.clips;

    auto [clip_result, milliseconds] =
        measure_repeated(parameters.repeats, [&request] { return clip(request); });

    result.closed_result = std::move(clip_result.closed);
    result.open_result = std::move(clip_result.open);
    result.metrics = base_metrics(result.subjects,
                                  result.clips,
                                  (std::max)(std::size_t{1}, parameters.repeats),
                                  "boolean");
    result.metrics.algorithm_ms = milliseconds;
    finish_metrics(result.metrics, result.closed_result, result.open_result);
    result.status = "ok";
    return result;
}

[[nodiscard]] auto run_offset(const demo_parameters& parameters) -> demo_result {
    demo_result result;
    result.subjects = make_polygon_grid(
        parameters.seed + 17U, parameters.path_count, parameters.vertex_count, 0, 0);

    offset_request64 request;
    request.paths = result.subjects;
    request.delta = parameters.offset_delta;
    request.join_type = JoinType::Round;
    request.end_type = EndType::Polygon;
    request.arc_tolerance = 0.0;

    auto [offset_result, milliseconds] =
        measure_repeated(parameters.repeats, [&request] { return offset(request); });

    result.closed_result = std::move(offset_result.closed);
    result.open_result = std::move(offset_result.open);
    result.metrics =
        base_metrics(result.subjects, {}, (std::max)(std::size_t{1}, parameters.repeats), "round");
    result.metrics.algorithm_ms = milliseconds;
    finish_metrics(result.metrics, result.closed_result, result.open_result);
    result.status = "ok";
    return result;
}

[[nodiscard]] auto run_rect_clip(const demo_parameters& parameters) -> demo_result {
    demo_result result;
    result.subjects = make_polygon_grid(
        parameters.seed + 29U, parameters.path_count, parameters.vertex_count, -80, -20);
    result.clip_rect = Rect64{80, 70, 820, 620};

    rect_clip_request64 request;
    request.rect = result.clip_rect;
    request.paths = result.subjects;

    const auto mode = parameters.rect_clip_mode;
    auto rect_result = rect_clip_result64{};
    auto milliseconds = 0.0;
    if (mode == demo_rect_clip_mode::prepared) {
        auto prepared = prepare_rect_clip_request(request);
        auto measured =
            measure_repeated(parameters.repeats, [&prepared] { return rect_clip(prepared); });
        rect_result = std::move(measured.first);
        milliseconds = measured.second;
    } else if (mode == demo_rect_clip_mode::immutable) {
        const auto immutable_paths = prepare_immutable_rect_clip_paths(request.paths);
        auto measured = measure_repeated(parameters.repeats, [&request, &immutable_paths] {
            return rect_clip(request.rect, immutable_paths);
        });
        rect_result = std::move(measured.first);
        milliseconds = measured.second;
    } else {
        auto measured = measure_repeated(parameters.repeats, [&request] { return rect_clip(request); });
        rect_result = std::move(measured.first);
        milliseconds = measured.second;
    }

    result.closed_result = std::move(rect_result.paths);
    result.metrics = base_metrics(result.subjects,
                                  {},
                                  (std::max)(std::size_t{1}, parameters.repeats),
                                  rect_clip_mode_label(mode));
    result.metrics.algorithm_ms = milliseconds;
    finish_metrics(result.metrics, result.closed_result, result.open_result);
    result.status = "ok";
    return result;
}

[[nodiscard]] auto run_triangulation(const demo_parameters& parameters) -> demo_result {
    demo_result result;
    result.subjects = make_triangulation_subject(parameters);

    triangulation_request64 request;
    request.paths = result.subjects;
    request.use_delaunay = parameters.use_delaunay;

    auto [triangulation_result, milliseconds] =
        measure_repeated(parameters.repeats, [&request] { return triangulate(request); });

    result.closed_result = std::move(triangulation_result.triangles);
    result.metrics = base_metrics(result.subjects,
                                  {},
                                  (std::max)(std::size_t{1}, parameters.repeats),
                                  parameters.use_delaunay ? "delaunay" : "sweep");
    result.metrics.algorithm_ms = milliseconds;
    finish_metrics(result.metrics, result.closed_result, result.open_result);
    result.status =
        triangulation_result.status == TriangulateResult::success ? "ok" : "triangulation failed";
    return result;
}

[[nodiscard]] auto run_batch_clip(const demo_parameters& parameters) -> demo_result {
    demo_result result;
    result.subjects = make_boolean_subjects(parameters);
    result.clips = make_boolean_clips(parameters);

    std::vector<clip_request64> requests;
    const auto request_count = bounded_count(parameters.path_count, 1U, 256U);
    requests.reserve(request_count);
    for (std::size_t index = 0; index < request_count; ++index) {
        clip_request64 request;
        request.clip_type = to_clip_type(parameters.operation);
        request.fill_rule = FillRule::NonZero;
        request.subjects = Paths64{result.subjects[index % result.subjects.size()]};
        request.clips = Paths64{result.clips[index % result.clips.size()]};
        requests.push_back(std::move(request));
    }

    auto [batch_result, milliseconds] =
        measure_repeated(parameters.repeats, [&requests] { return clip_batch(requests); });

    for (auto& item : batch_result) {
        for (auto& path : item.closed) { result.closed_result.push_back(std::move(path)); }
        for (auto& path : item.open) { result.open_result.push_back(std::move(path)); }
    }
    result.metrics = base_metrics(result.subjects,
                                  result.clips,
                                  (std::max)(std::size_t{1}, parameters.repeats),
                                  "batch");
    result.metrics.algorithm_ms = milliseconds;
    finish_metrics(result.metrics, result.closed_result, result.open_result);
    result.status = "ok";
    return result;
}

}  // namespace

auto run_demo(const demo_parameters& parameters) -> demo_result {
    switch (parameters.scene) {
        case demo_scene::boolean_clip:
            return run_boolean_clip(parameters);
        case demo_scene::offset:
            return run_offset(parameters);
        case demo_scene::rect_clip:
            return run_rect_clip(parameters);
        case demo_scene::triangulation:
            return run_triangulation(parameters);
        case demo_scene::batch_clip:
            return run_batch_clip(parameters);
    }
    return run_boolean_clip(parameters);
}

}  // namespace clipper2next::demo
