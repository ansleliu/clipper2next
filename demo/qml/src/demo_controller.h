#pragma once

#include "demo_model.h"

#include <QObject>
#include <QString>
#include <QVariant>

namespace clipper2next::demo {

class DemoController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString scene READ scene WRITE setScene NOTIFY parametersChanged)
    Q_PROPERTY(QString operation READ operation WRITE setOperation NOTIFY parametersChanged)
    Q_PROPERTY(QString rectClipMode READ rectClipMode WRITE setRectClipMode NOTIFY parametersChanged)
    Q_PROPERTY(int seed READ seed WRITE setSeed NOTIFY parametersChanged)
    Q_PROPERTY(int pathCount READ pathCount WRITE setPathCount NOTIFY parametersChanged)
    Q_PROPERTY(int vertexCount READ vertexCount WRITE setVertexCount NOTIFY parametersChanged)
    Q_PROPERTY(double offsetDelta READ offsetDelta WRITE setOffsetDelta NOTIFY parametersChanged)
    Q_PROPERTY(int repeats READ repeats WRITE setRepeats NOTIFY parametersChanged)
    Q_PROPERTY(bool useDelaunay READ useDelaunay WRITE setUseDelaunay NOTIFY parametersChanged)
    Q_PROPERTY(QVariantList subjects READ subjects NOTIFY geometryChanged)
    Q_PROPERTY(QVariantList clips READ clips NOTIFY geometryChanged)
    Q_PROPERTY(QVariantList results READ results NOTIFY geometryChanged)
    Q_PROPERTY(QVariantMap clipRect READ clipRect NOTIFY geometryChanged)
    Q_PROPERTY(double algorithmMs READ algorithmMs NOTIFY metricsChanged)
    Q_PROPERTY(double renderMs READ renderMs NOTIFY metricsChanged)
    Q_PROPERTY(int inputPathCount READ inputPathCount NOTIFY metricsChanged)
    Q_PROPERTY(int inputPointCount READ inputPointCount NOTIFY metricsChanged)
    Q_PROPERTY(int outputPathCount READ outputPathCount NOTIFY metricsChanged)
    Q_PROPERTY(int outputPointCount READ outputPointCount NOTIFY metricsChanged)
    Q_PROPERTY(double outputArea READ outputArea NOTIFY metricsChanged)
    Q_PROPERTY(QString status READ status NOTIFY metricsChanged)
    Q_PROPERTY(QString executionMode READ executionMode NOTIFY metricsChanged)

public:
    explicit DemoController(QObject* parent = nullptr);

    [[nodiscard]] auto scene() const -> QString;
    auto setScene(const QString& value) -> void;

    [[nodiscard]] auto operation() const -> QString;
    auto setOperation(const QString& value) -> void;

    [[nodiscard]] auto rectClipMode() const -> QString;
    auto setRectClipMode(const QString& value) -> void;

    [[nodiscard]] auto seed() const -> int;
    auto setSeed(int value) -> void;

    [[nodiscard]] auto pathCount() const -> int;
    auto setPathCount(int value) -> void;

    [[nodiscard]] auto vertexCount() const -> int;
    auto setVertexCount(int value) -> void;

    [[nodiscard]] auto offsetDelta() const -> double;
    auto setOffsetDelta(double value) -> void;

    [[nodiscard]] auto repeats() const -> int;
    auto setRepeats(int value) -> void;

    [[nodiscard]] auto useDelaunay() const -> bool;
    auto setUseDelaunay(bool value) -> void;

    [[nodiscard]] auto subjects() const -> QVariantList;
    [[nodiscard]] auto clips() const -> QVariantList;
    [[nodiscard]] auto results() const -> QVariantList;
    [[nodiscard]] auto clipRect() const -> QVariantMap;

    [[nodiscard]] auto algorithmMs() const -> double;
    [[nodiscard]] auto renderMs() const -> double;
    [[nodiscard]] auto inputPathCount() const -> int;
    [[nodiscard]] auto inputPointCount() const -> int;
    [[nodiscard]] auto outputPathCount() const -> int;
    [[nodiscard]] auto outputPointCount() const -> int;
    [[nodiscard]] auto outputArea() const -> double;
    [[nodiscard]] auto status() const -> QString;
    [[nodiscard]] auto executionMode() const -> QString;

    Q_INVOKABLE void run();
    Q_INVOKABLE void newSample();
    Q_INVOKABLE void resetView();
    Q_INVOKABLE void setRenderMilliseconds(double value);

signals:
    void parametersChanged();
    void geometryChanged();
    void metricsChanged();
    void viewResetRequested();

private:
    auto emitParameterChangeAndRun() -> void;

    demo_parameters parameters_{};
    demo_result result_{};
    QVariantList subjects_;
    QVariantList clips_;
    QVariantList results_;
    QVariantMap clip_rect_;
    double render_ms_{};
};

}  // namespace clipper2next::demo
