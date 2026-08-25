#include "demo_controller.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QtGlobal>
#include <QtQml>

auto main(int argc, char* argv[]) -> int {
    QGuiApplication app(argc, argv);

    qmlRegisterType<clipper2next::demo::DemoController>(
        "Clipper2NextDemo", 1, 0, "DemoController");

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(
        "initialDemoScene", qEnvironmentVariable("CLIPPER2NEXT_DEMO_SCENE"));
    engine.rootContext()->setContextProperty(
        "initialDemoOperation", qEnvironmentVariable("CLIPPER2NEXT_DEMO_OPERATION"));
    engine.rootContext()->setContextProperty(
        "initialDemoRectMode", qEnvironmentVariable("CLIPPER2NEXT_DEMO_RECT_MODE"));
    engine.loadFromModule("Clipper2NextDemo", "Main");
    if (engine.rootObjects().isEmpty()) { return 1; }

    return QGuiApplication::exec();
}
