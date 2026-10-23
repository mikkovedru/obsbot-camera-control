# Contributing

## Building

```bash
./build.sh build --confirm
```

Binary ends up in `./bin/obsbot-gui`. Run it directly to test.

For a clean rebuild:
```bash
./build.sh clean --confirm
./build.sh build --confirm
```

### Headless regression tests

```bash
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --target autofocus-tests exposure-tests virtual-output-tests preview-output-tests
ctest --test-dir build --output-on-failure
```

These tests use an offscreen Qt platform and a temporary configuration directory;
they do not connect to or change camera hardware.

The OpenGL export test requires a display and runs only when explicitly enabled:

```bash
OBSBOT_TEST_OPENGL=1 QT_SCALE_FACTOR=2 ./build/preview-output-tests
```

It uses synthetic video frames to check high-DPI export, continued output while
hidden, and repeated GL cleanup. CTest reports it skipped without the opt-in.

## Dependencies

**Build**: cmake, make, gcc/g++, qt6-base, qt6-multimedia, Qt6 OpenGLWidgets, pkg-config

On Arch: `pacman -S cmake qt6-base qt6-multimedia`

On Debian/Ubuntu:
`apt install build-essential cmake qt6-base-dev qt6-multimedia-dev libqt6opengl6-dev pkg-config`

## Making Changes

1. Fork and branch from `main`
2. Make your changes in `src/`
3. Build and test with your camera
4. Submit a PR

### Code Style

- Follow what's already there — this isn't the project for style debates
- Use `blockSignals()` when setting widget values programmatically (avoids feedback loops)
- Don't add custom widget styling — respect the user's Qt/KDE theme

### What to Include in Your PR

- What you changed and why
- Which camera model you tested with
- If it's a UI change, a screenshot helps

## Project Structure

```
src/
├── gui/           # Qt GUI — most changes happen here
│   ├── MainWindow.cpp/h
│   ├── TrackingControlWidget.cpp/h    # Auto-framing + PTZ
│   ├── PTZControlWidget.cpp/h         # Preset management
│   ├── CameraSettingsWidget.cpp/h     # Image settings
│   └── CameraPreviewWidget.cpp/h      # Live preview
├── cli/           # CLI tool
└── common/        # Shared code (config, camera controller)
```

## Camera-Specific Notes

- `CameraController::isTiny2Family()` determines which advanced features are shown
- Meet 2 gets simpler tracking UI (on/off only)
- Tiny 2 family gets full AI mode selector
- If your change only applies to certain camera models, say so in the PR

## SDK

The OBSBOT SDK is a closed-source library — we get headers and a compiled `.so`, no source. If something doesn't work at the SDK level, we work around it. Check `sdk/include/dev/dev.hpp` for the API surface.

### Preview timing benchmark

Build the optional benchmark before changing the preview pipeline:

```bash
cmake -S . -B build -DOBSBOT_BUILD_BENCHMARKS=ON
cmake --build build --target preview-benchmark
./build/preview-benchmark --seconds 5 --output /tmp/preview-timing.json
./build/preview-benchmark --camera --seconds 5
./build/preview-benchmark --camera --session-first --seconds 8
./build/preview-benchmark --camera --poll-camera-state --seconds 8
```

A display with OpenGL support is required. The default uses synthetic 1080p60
frames. `--camera` briefly displays the connected OBSBOT feed without saving
images or applying camera controls. `--poll-camera-state` adds the controller's
read-only SDK polling after a longer warm-up. `--export` attaches a processed-frame
consumer. `--session-first` reproduces the original application startup order
by attaching the camera to the session before selecting its capture format.
Use `--width`, `--height` and `--fps` to compare capture formats.

The JSON records delivered and painted frame rates, delivery/paint p50 and p95,
GUI timer gaps, and optional SDK poll times. Paint timings measure CPU submission,
not completed GPU work. Compare the same source, size, display and build settings
before and after each independent change. This benchmark is not an automatic test
and cannot guarantee camera frame rate on other hardware.
