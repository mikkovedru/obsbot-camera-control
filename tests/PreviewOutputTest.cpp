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
    QImage input(160, 120, QImage::Format_RGBA8888);
    const QColor colors[] = {Qt::red, Qt::green, Qt::blue, Qt::yellow};
    for (int y = 0; y < input.height(); ++y)
        for (int x = 0; x < input.width(); ++x)
            input.setPixelColor(x, y, colors[(y >= 60) * 2 + (x >= 80)]);
    QVideoFrame frame(QVideoFrameFormat(input.size(), QVideoFrameFormat::Format_RGBA8888));
    require(frame.map(QVideoFrame::WriteOnly), "Cannot map synthetic video frame");
    for (int y = 0; y < input.height(); ++y)
        std::memcpy(frame.bits(0) + y * frame.bytesPerLine(0), input.constScanLine(y), input.width() * 4);
    frame.unmap();

    for (int iteration = 0; iteration < 3; ++iteration) {
        auto *widget = new FilterPreviewWidget;
        widget->resize(320, 240);
        widget->show();
        require(waitUntil([widget]() { return widget->isValid(); }),
                "OpenGL context must initialize on the selected display");
        require(widget->devicePixelRatioF() > 1.5, "Run this test with QT_SCALE_FACTOR=2");
        QImage output;
        int emitted = 0;
        QObject::connect(widget, &FilterPreviewWidget::processedFrameReady,
                         [&](const QImage &image) { output = image; ++emitted; });
        widget->updateVideoFrame(frame);
        require(waitUntil([&output]() { return !output.isNull(); }),
                "Preview must produce an exported frame before checking its dimensions");
        require(output.size() == input.size(), "Export must retain the full input dimensions");
        for (int quadrant = 0; quadrant < 4; ++quadrant) {
            const QColor pixel = output.pixelColor(40 + (quadrant % 2) * 80, 30 + (quadrant / 2) * 60);
            const auto &expected = colors[quadrant];
            require(std::abs(pixel.red() - expected.red()) < 5 &&
                    std::abs(pixel.green() - expected.green()) < 5 &&
                    std::abs(pixel.blue() - expected.blue()) < 5,
                    "All four quadrants must survive high-DPI export without cropping");
        }
        widget->hide();
        waitForEvents(30);
        const int before = emitted;
        widget->updateVideoFrame(frame);
        require(emitted == before + 1, "Hidden preview must keep producing virtual-camera frames without paint events");
        delete widget;
        waitForEvents(10);
    }
    qInfo("High-DPI export, hidden-window output, and repeated GL teardown passed");
}
