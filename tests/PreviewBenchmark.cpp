#include "FilterPreviewWidget.h"
#include "CameraController.h"
#include <QApplication>
#include <QCamera>
#include <QCameraDevice>
#include <QCommandLineParser>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMediaCaptureSession>
#include <QMediaDevices>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QTimer>
#include <QVideoFrameFormat>
#include <QVideoSink>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <memory>
#include <vector>

static double percentile(std::vector<double> values, double p)
{
    if (values.empty()) return 0;
    std::sort(values.begin(), values.end());
    return values[static_cast<size_t>((values.size() - 1) * p)];
}

class MeasuredPreview : public FilterPreviewWidget
{
public:
    std::vector<double> paints;
protected:
    void paintGL() override {
        QElapsedTimer timer;
        timer.start();
        FilterPreviewWidget::paintGL();
        paints.push_back(timer.nsecsElapsed() / 1e6);
    }
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOptions({{"width", "Capture width", "pixels", "1920"},
                       {"height", "Capture height", "pixels", "1080"},
                       {"fps", "Requested camera rate; synthetic uses the same timer rate", "fps", "60"},
                       {"seconds", "Measurement duration after warm-up", "seconds", "5"},
                       {"camera", "Read the connected OBSBOT camera instead of synthetic RGBA frames"},
                       {"session-first", "Attach the capture session before selecting the format, matching the original app startup"},
                       {"export", "Connect a processed-frame consumer"},
                       {"poll-camera-state", "Include the app's SDK state polling without changing camera controls"},
                       {"output", "Write JSON measurements, never camera images", "path"}});
    parser.process(app);
    const QSize size(parser.value("width").toInt(), parser.value("height").toInt());
    const int fps = parser.value("fps").toInt();
    const int seconds = parser.value("seconds").toInt();
    if (size.isEmpty() || fps <= 0 || seconds <= 0) return 2;

    MeasuredPreview preview;
    preview.resize(800, 450);
    preview.show();
    std::vector<double> deliveries;
    int frames = 0, exports = 0;
    QString pixelFormat, renderer;
    QElapsedTimer measurement;
    QTimer producer, warmup, stop;
    warmup.setSingleShot(true);
    stop.setSingleShot(true);
    bool measuring = false;
    std::vector<double> polls, eventGaps;
    QElapsedTimer heartbeatClock;
    heartbeatClock.start();
    QTimer heartbeat, pollTimer;
    QObject::connect(&heartbeat, &QTimer::timeout, [&]() {
        const double gap = heartbeatClock.nsecsElapsed() / 1e6;
        heartbeatClock.restart();
        if (measuring) eventGaps.push_back(gap);
    });
    heartbeat.start(16);
    std::unique_ptr<CameraController> controller;
    if (parser.isSet("poll-camera-state")) {
        controller = std::make_unique<CameraController>();
        controller->connectToCamera();
        QObject::connect(&pollTimer, &QTimer::timeout, [&]() {
            if (!controller->isConnected()) return;
            QElapsedTimer timer;
            timer.start();
            QMediaDevices::videoInputs();
            controller->getSerialsByDevicePath();
            controller->getCurrentState();
            if (measuring) polls.push_back(timer.nsecsElapsed() / 1e6);
        });
        pollTimer.start(2000);
    }
    if (parser.isSet("export")) {
        preview.setFrameExportEnabled(true);
        QObject::connect(&preview, &FilterPreviewWidget::processedFrameReady,
                         [&](const QImage &) { if (measuring) ++exports; });
    }
    auto deliver = [&](const QVideoFrame &frame) {
        if (!frame.isValid()) return;
        if (pixelFormat.isEmpty()) pixelFormat = QVideoFrameFormat::pixelFormatToString(frame.pixelFormat());
        QElapsedTimer timer;
        timer.start();
        preview.updateVideoFrame(frame);
        if (measuring) {
            deliveries.push_back(timer.nsecsElapsed() / 1e6);
            ++frames;
        }
    };

    QCamera *camera = nullptr;
    QMediaCaptureSession session;
    QVideoSink sink;
    QVideoFrame synthetic;
    if (parser.isSet("camera")) {
        for (const auto &device : QMediaDevices::videoInputs()) {
            if (!device.description().contains("OBSBOT", Qt::CaseInsensitive) ||
                device.description().contains("Virtual", Qt::CaseInsensitive)) continue;
            QCameraFormat chosen;
            for (const auto &format : device.videoFormats()) {
                if (format.resolution() == size && std::abs(format.maxFrameRate() - fps) < 1 &&
                    (chosen.isNull() || format.pixelFormat() == QVideoFrameFormat::Format_Jpeg)) chosen = format;
            }
            if (chosen.isNull()) continue;
            camera = new QCamera(device, &app);
            if (parser.isSet("session-first")) session.setCamera(camera);
            camera->setCameraFormat(chosen);
            if (!parser.isSet("session-first")) session.setCamera(camera);
            session.setVideoSink(&sink);
            QObject::connect(&sink, &QVideoSink::videoFrameChanged, &preview, deliver);
            QObject::connect(camera, &QCamera::errorOccurred, [&](QCamera::Error, const QString &error) {
                std::cerr << error.toStdString() << '\n'; app.exit(3);
            });
            if (controller && !controller->isConnected()) {
                QObject::connect(controller.get(), &CameraController::cameraConnected, camera,
                                 [camera]() { camera->start(); });
            } else {
                camera->start();
            }
            break;
        }
        if (!camera) { std::cerr << "Requested OBSBOT format unavailable\n"; return 2; }
    } else {
        synthetic = QVideoFrame(QVideoFrameFormat(size, QVideoFrameFormat::Format_RGBA8888));
        if (!synthetic.map(QVideoFrame::WriteOnly)) return 2;
        for (int y = 0; y < size.height(); ++y) {
            auto *row = synthetic.bits(0) + y * synthetic.bytesPerLine(0);
            for (int x = 0; x < size.width(); ++x) {
                row[x * 4] = x % 256; row[x * 4 + 1] = y % 256;
                row[x * 4 + 2] = (x + y) % 256; row[x * 4 + 3] = 255;
            }
        }
        synthetic.unmap();
        QObject::connect(&producer, &QTimer::timeout, [&]() { deliver(synthetic); });
        producer.setTimerType(Qt::PreciseTimer);
        producer.start(std::max(1, 1000 / fps));
    }
    QObject::connect(&warmup, &QTimer::timeout, [&]() {
        preview.makeCurrent();
        if (QOpenGLContext::currentContext()) {
            renderer = reinterpret_cast<const char *>(preview.context()->functions()->glGetString(GL_RENDERER));
        }
        preview.doneCurrent();
        preview.paints.clear();
        measuring = true;
        measurement.start();
        stop.start(seconds * 1000);
    });
    QObject::connect(&stop, &QTimer::timeout, [&]() {
        measuring = false;
        producer.stop();
        const double elapsed = measurement.elapsed() / 1000.;
        QJsonObject result{{"source", camera ? "camera" : "synthetic"}, {"width", size.width()}, {"height", size.height()},
                           {"session_first", parser.isSet("session-first")}, {"requested_fps", fps}, {"seconds", elapsed}, {"delivered_frames", frames},
                           {"delivered_fps", frames / elapsed}, {"painted_frames", int(preview.paints.size())},
                           {"painted_fps", preview.paints.size() / elapsed}, {"exported_frames", exports},
                           {"export_consumer", parser.isSet("export")}, {"pixel_format", pixelFormat}, {"renderer", renderer},
                           {"delivery_p50_ms", percentile(deliveries, .5)}, {"delivery_p95_ms", percentile(deliveries, .95)},
                           {"paint_p50_ms", percentile(preview.paints, .5)}, {"paint_p95_ms", percentile(preview.paints, .95)}};
        result.insert("poll_state", bool(controller));
        result.insert("state_poll_p50_ms", percentile(polls, .5));
        result.insert("state_poll_p95_ms", percentile(polls, .95));
        result.insert("gui_gap_max_ms", percentile(eventGaps, 1));
        result.insert("gui_gap_p95_ms", percentile(eventGaps, .95));
        const auto json = QJsonDocument(result).toJson();
        std::cout << json.constData();
        if (parser.isSet("output")) { QFile file(parser.value("output")); if (!file.open(QIODevice::WriteOnly) || file.write(json) != json.size()) { app.exit(4); return; } }
        app.exit(frames > 0 ? 0 : 5);
    });
    warmup.start(controller ? 6000 : 1000);
    const int status = app.exec();
    if (camera) camera->stop();
    session.setCamera(nullptr);
    session.setVideoSink(nullptr);
    return status;
}
