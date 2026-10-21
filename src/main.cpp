#include "Editor.h"
#include "ScoreView.h"

#include <QGuiApplication>
#include <QFontDatabase>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QtQml>

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("Music as Becoming"));
    app.setOrganizationName(QStringLiteral("Music as Becoming"));
    QFontDatabase::addApplicationFont(QStringLiteral(":/assets/Bravura.otf"));
    qmlRegisterType<music::ScoreView>("Music", 1, 0, "ScoreView");
    music::Editor editor;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("appEditor", &editor);
    engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
    if (engine.rootObjects().isEmpty()) return 1;
    return app.exec();
}
