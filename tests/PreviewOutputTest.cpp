#include "FilterPreviewWidget.h"
#include <QApplication>
#include <QElapsedTimer>
#include <QThread>
#include <QVideoFrameFormat>
#include <QDebug>
#include <cstring>
#include <functional>

static void require(bool condition, const char *message)
{
    if (!condition) qFatal("%s", message);
}

static void waitForEvents(int milliseconds)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < milliseconds) {
        QApplication::processEvents();
        QThread::msleep(5);
    }
}

static bool waitUntil(const std::function<bool()> &condition, int timeoutMs = 3000)
{
    QElapsedTimer timer;
    timer.start();
    while (!condition() && timer.elapsed() < timeoutMs) {
        QApplication::processEvents();
        QThread::msleep(5);
    }
    return condition();
}

int main(int argc, char **argv)
{
    if (!qEnvironmentVariableIsSet("OBSBOT_TEST_OPENGL")) {
        qInfo("Set OBSBOT_TEST_OPENGL=1 with an OpenGL-capable display to run this test");
        return 77;
    }
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    for (int iteration = 0; iteration < 3; ++iteration) {
        auto *widget = new FilterPreviewWidget;
        widget->resize(320, 240);
        widget->show();
        require(waitUntil([widget]() { return widget->isValid(); }),
                "OpenGL context must initialize on the selected display");
        delete widget;
        waitForEvents(10);
    }
    qInfo("Repeated OpenGL initialization and teardown passed");
}
