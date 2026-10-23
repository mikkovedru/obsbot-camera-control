# Tiny 2 preview performance, 2026-10-24

The near-one-frame-per-second preview was reproduced and fixed. Two independent
changes were measured separately. With both changes, the full window delivered
and painted 60.02 fps at 1920×1080@60, compared with 0.78 fps before either change.
These are short local measurements, not a guarantee for every camera or system.

## Capture startup was the main trigger

The application attached its QCamera to QMediaCaptureSession before selecting
the requested capture format. Selecting 1080p60 after attachment caused severe
frame loss on this machine's Qt 6.4.2 GStreamer/PipeWire capture path. Selecting
the same format before attachment removed the stalls.

The slow run showed negotiation warnings involving a different default resolution
(1920×1440), QoS frame drops, and roughly one-second gaps in frame timestamps.
The GUI event loop remained responsive. Changing only startup order reproduced
and removed the slowdown in both the isolated benchmark and a full-window probe.
This confirms the startup sequence as a trigger here. The exact internal
Qt/PipeWire defect has not been established. A GStreamer bus-overflow warning
also appeared in a fast run, so that warning alone does not establish the cause.

| Live 1080p60 test | Original order | Format selected first | Improvement |
| --- | ---: | ---: | ---: |
| Isolated preview, delivered fps | 0.77 | 40.12 | x52.4 |
| Full window, delivered fps | 0.78 | 46.15 | x59.0 |
| Full window, painted fps | 0.78 | 45.81 | x58.6 |

The fix is [7071035](https://github.com/mikkovedru/obsbot-camera-control/commit/707103539eae42de2f7b64291aec7d3adadc64d0).
The sequence predates the current integration work: it is present in Zoe Gates's
[preview quality change](https://github.com/aaronsb/obsbot-camera-control/commit/a870cfb8ae2d1568770537b23add5de38d3661ea)
in the [upstream repository](https://github.com/aaronsb/obsbot-camera-control).
No external PR was imported for this performance fix. The new fix commits are
local; their GitHub links will resolve after publication.

## Unused processed-frame export was a second cost

Even with virtual-camera output disabled, every captured frame was uploaded,
rendered into a full-resolution offscreen framebuffer, read back to CPU memory,
and vertically copied for processed-frame output. Normal on-screen preview did
not need that output.

The filter now exports continuously only while a virtual-camera consumer is
enabled. A snapshot requests one export from the next valid captured frame.
Stopping preview cancels pending snapshots. Enabled output still runs during
capture delivery, preserving streaming when paint events stop in hidden windows.
Streamer failure, disable, and destruction turn continuous exports off.

| Live test with startup already fixed | Before conditional export | After | Improvement |
| --- | ---: | ---: | ---: |
| Isolated preview, delivered fps | 40.12 | 59.99 | x1.50 |
| Isolated preview, painted fps | 37.48 | 59.86 | x1.60 |
| Delivery p50 | 20.35 ms | 5.36 ms | x3.80 lower |
| Delivery p95 | 28.99 ms | 6.44 ms | x4.50 lower |
| Paint submission p50 | 0.023 ms | 2.582 ms | Work moves into paint |
| Paint submission p95 | 0.031 ms | 3.212 ms | Work moves into paint |
| Full window, delivered fps | 46.15 | 60.02 | x1.30 |
| Full window, painted fps | 45.81 | 60.02 | x1.31 |
| Full window, largest GUI timer gap | 96.93 ms | 25.64 ms | x3.78 lower |

Frame conversion remains in delivery; texture upload moves into painting when
export is off. Delivery timings alone therefore overstate the reduction in total
rendering work. Paint timings measure CPU submission, not completed GPU work.
The p50/p95 values of separate stages must not be added and called an end-to-end
percentile. Throughput and full-window paint counts corroborate the useful gain.

A separate synthetic RGBA test delivered about 61 fps both before and after this
change. Its delivery p50 fell from 14.72 to 2.53 ms (x5.82), and p95 from 18.96 to
5.63 ms (x3.37), while paint submission rose from 0.020/0.030 ms to 5.173/10.257 ms
(p50/p95). It does not demonstrate a throughput gain for synthetic frames.

Continuous export remains more expensive: with it enabled, the live benchmark
produced 427 full-resolution processed frames at 55.70 fps. That check consumes
frames in memory; it does not include conversion/writing to a real v4l2loopback
consumer. The full-window probe requested one snapshot and received exactly one
1920×1080 image in memory, without writing a camera image to disk.

The fix is [1d18c5c](https://github.com/mikkovedru/obsbot-camera-control/commit/1d18c5c230b8756b6ef3ced21fd78b823b658baa).
The original processed-frame path came from Zoe Gates's
[video effects change](https://github.com/aaronsb/obsbot-camera-control/commit/4f842cec558365170507d14401000814c8658af5).
The later [hidden-window integration](https://github.com/mikkovedru/obsbot-camera-control/commit/8b1604aae5411cf2d2abcc9b5a3936e8dca86c5f)
moved export into capture delivery. This fix preserves its behavior when output
is actually enabled. Existing imported-PR provenance remains in
[the integration report](PR_REVIEW_2026-10-22.md).

## Method, limitations, and reproduction

Measurements used the connected Tiny 2, MJPEG capture at 1920×1080@60, decoded
YUV420P frames, an AMD Radeon RX 6600 XT with hardware-accelerated Mesa, X11,
and the existing build configuration with CMAKE_BUILD_TYPE unset. No compiler
optimization change was included in these gains. Runs were sequential.
Rates varied between runs: earlier format-first runs reached about 53–54 fps,
while the matched comparison above reached 40–46 fps. System load was not
controlled, so the reason for that smaller variation has not been isolated.

The full-window probe instantiated MainWindow, used temporary configuration,
suppressed automatic camera-setting writes, waited for SDK discovery, enabled
preview, and counted sink deliveries and widget paint events after warm-up.
The original and format-only probes used the same procedure. The final probe
also requested one in-memory snapshot. It did not validate the user's saved
settings or normal automatic setting application. Read-only SDK polling remained
active; isolated polling took about 0.096 ms in an earlier run and did not
reproduce the stalls. Live Wayland behavior was not tested.

One post-change live benchmark produced no frames and exited with status 5.
It is retained in the data as `rejected_no_frames`, excluded from performance
ratios. The full-window run and a subsequent standalone retry delivered 60 fps.
Intermittent capture startup still warrants observation. Qt/GStreamer also emits
an object-unref warning on some teardown runs; this investigation does not fix it.

[Raw measurements](benchmarks/preview-2026-10-24.json) retain elapsed times, frame
counts, and timing percentiles. Reproduce the original ordering and corrected
ordering without changing production code:

```bash
cmake -S . -B build -DOBSBOT_BUILD_BENCHMARKS=ON
cmake --build build --target preview-benchmark
./build/preview-benchmark --camera --session-first --seconds 8
./build/preview-benchmark --camera --seconds 8
./build/preview-benchmark --camera --export --seconds 8
./build/preview-benchmark --camera --poll-camera-state --seconds 8
```

On the final code, `--session-first` deliberately restores the original startup
order while retaining the conditional-export change. Historical baseline numbers
above were recorded before that second change. For an exact comparison, use the
benchmark infrastructure commit before the implementation changes.

All four CTest tests pass with `OBSBOT_TEST_OPENGL=1 QT_QPA_PLATFORM=xcb`.
The display test uses scale factor 2 and covers visible preview without export,
one-shot/cancelled snapshots, disabled exports, high-DPI output quadrants,
hidden-window continuous output, and repeated GL teardown. Both GUI and CLI were
rebuilt using `./build.sh build --confirm`.

The rebuilt executables are `bin/obsbot-gui` and `bin/obsbot-cli`. The desktop
launcher still targets the older installed `~/.local/bin/obsbot-gui`; use the
repository binary to try these fixes. No installed launcher or binary was changed.
