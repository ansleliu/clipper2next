#include "demo_controller.h"

#include "path_adapter.h"

#include <algorithm>

namespace clipper2next::demo {
namespace {

[[nodiscard]] auto scene_to_string(demo_scene scene) -> QString {
    switch (scene) {
        case demo_scene::boolean_clip:
            return "boolean_clip";
        case demo_scene::offset:
            return "offset";
        case demo_scene::rect_clip:
            return "rect_clip";
        case demo_scene::triangulation:
            return "triangulation";
        case demo_scene::batch_clip:
            return "batch_clip";
    }
    return "boolean_clip";
}

[[nodiscard]] auto scene_from_string(const QString& value) -> demo_scene {
    if (value == "offset") { return demo_scene::offset; }
    if (value == "rect_clip") { return demo_scene::rect_clip; }
    if (value == "triangulation") { return demo_scene::triangulation; }
    if (value == "batch_clip") { return demo_scene::batch_clip; }
    return demo_scene::boolean_clip;
}

[[nodiscard]] auto operation_to_string(demo_operation operation) -> QString {
    switch (operation) {
        case demo_operation::intersection:
            return "intersection";
        case demo_operation::union_op:
            return "union";
        case demo_operation::difference:
            return "difference";
        case demo_operation::xor_op:
            return "xor";
    }
    return "union";
}

[[nodiscard]] auto operation_from_string(const QString& value) -> demo_operation {
    if (value == "intersection") { return demo_operation::intersection; }
    if (value == "difference") { return demo_operation::difference; }
    if (value == "xor") { return demo_operation::xor_op; }
    return demo_operation::union_op;
}

[[nodiscard]] auto rect_clip_mode_to_string(demo_rect_clip_mode mode) -> QString {
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

[[nodiscard]] auto rect_clip_mode_from_string(const QString& value) -> demo_rect_clip_mode {
    if (value == "prepared") { return demo_rect_clip_mode::prepared; }
    if (value == "immutable") { return demo_rect_clip_mode::immutable; }
    return demo_rect_clip_mode::direct;
}

[[nodiscard]] auto bounded_int(int value, int low, int high) -> int {
    return (std::min)((std::max)(value, low), high);
}

}  // namespace

DemoController::DemoController(QObject* parent)
    : QObject(parent) {
    run();
}

auto DemoController::scene() const -> QString {
    return scene_to_string(parameters_.scene);
}

auto DemoController::setScene(const QString& value) -> void {
    const auto next = scene_from_string(value);
    if (parameters_.scene == next) { return; }
    parameters_.scene = next;
    emitParameterChangeAndRun();
}

auto DemoController::operation() const -> QString {
    return operation_to_string(parameters_.operation);
}

auto DemoController::setOperation(const QString& value) -> void {
    const auto next = operation_from_string(value);
    if (parameters_.operation == next) { return; }
    parameters_.operation = next;
    emitParameterChangeAndRun();
}

auto DemoController::rectClipMode() const -> QString {
    return rect_clip_mode_to_string(parameters_.rect_clip_mode);
}

auto DemoController::setRectClipMode(const QString& value) -> void {
    const auto next = rect_clip_mode_from_string(value);
    if (parameters_.rect_clip_mode == next) { return; }
    parameters_.rect_clip_mode = next;
    emitParameterChangeAndRun();
}

auto DemoController::seed() const -> int {
    return static_cast<int>(parameters_.seed);
}

auto DemoController::setSeed(int value) -> void {
    const auto next = static_cast<std::uint32_t>(bounded_int(value, 1, 999999));
    if (parameters_.seed == next) { return; }
    parameters_.seed = next;
    emitParameterChangeAndRun();
}

auto DemoController::pathCount() const -> int {
    return static_cast<int>(parameters_.path_count);
}

auto DemoController::setPathCount(int value) -> void {
    const auto next = static_cast<std::size_t>(bounded_int(value, 1, 256));
    if (parameters_.path_count == next) { return; }
    parameters_.path_count = next;
    emitParameterChangeAndRun();
}

auto DemoController::vertexCount() const -> int {
    return static_cast<int>(parameters_.vertex_count);
}

auto DemoController::setVertexCount(int value) -> void {
    const auto next = static_cast<std::size_t>(bounded_int(value, 3, 512));
    if (parameters_.vertex_count == next) { return; }
    parameters_.vertex_count = next;
    emitParameterChangeAndRun();
}

auto DemoController::offsetDelta() const -> double {
    return parameters_.offset_delta;
}

auto DemoController::setOffsetDelta(double value) -> void {
    const auto next = (std::min)((std::max)(value, -160.0), 160.0);
    if (parameters_.offset_delta == next) { return; }
    parameters_.offset_delta = next;
    emitParameterChangeAndRun();
}

auto DemoController::repeats() const -> int {
    return static_cast<int>(parameters_.repeats);
}

auto DemoController::setRepeats(int value) -> void {
    const auto next = static_cast<std::size_t>(bounded_int(value, 1, 1000));
    if (parameters_.repeats == next) { return; }
    parameters_.repeats = next;
    emitParameterChangeAndRun();
}

auto DemoController::useDelaunay() const -> bool {
    return parameters_.use_delaunay;
}

auto DemoController::setUseDelaunay(bool value) -> void {
    if (parameters_.use_delaunay == value) { return; }
    parameters_.use_delaunay = value;
    emitParameterChangeAndRun();
}

auto DemoController::subjects() const -> QVariantList {
    return subjects_;
}

auto DemoController::clips() const -> QVariantList {
    return clips_;
}

auto DemoController::results() const -> QVariantList {
    return results_;
}

auto DemoController::clipRect() const -> QVariantMap {
    return clip_rect_;
}

auto DemoController::algorithmMs() const -> double {
    return result_.metrics.algorithm_ms;
}

auto DemoController::renderMs() const -> double {
    return render_ms_;
}

auto DemoController::inputPathCount() const -> int {
    return static_cast<int>(result_.metrics.input_path_count);
}

auto DemoController::inputPointCount() const -> int {
    return static_cast<int>(result_.metrics.input_point_count);
}

auto DemoController::outputPathCount() const -> int {
    return static_cast<int>(result_.metrics.output_path_count);
}

auto DemoController::outputPointCount() const -> int {
    return static_cast<int>(result_.metrics.output_point_count);
}

auto DemoController::outputArea() const -> double {
    return result_.metrics.output_area;
}

auto DemoController::status() const -> QString {
    return QString::fromStdString(result_.status);
}

auto DemoController::executionMode() const -> QString {
    return QString::fromStdString(result_.metrics.execution_mode);
}

void DemoController::run() {
    result_ = run_demo(parameters_);
    subjects_ = paths_to_variant(result_.subjects);
    clips_ = paths_to_variant(result_.clips);
    results_ = paths_to_variant(result_.closed_result);
    clip_rect_ = rect_to_variant(result_.clip_rect);
    emit geometryChanged();
    emit metricsChanged();
}

void DemoController::newSample() {
    parameters_.seed += 1U;
    emit parametersChanged();
    run();
}

void DemoController::resetView() {
    emit viewResetRequested();
}

void DemoController::setRenderMilliseconds(double value) {
    render_ms_ = (std::max)(0.0, value);
    emit metricsChanged();
}

auto DemoController::emitParameterChangeAndRun() -> void {
    emit parametersChanged();
    run();
}

}  // namespace clipper2next::demo
