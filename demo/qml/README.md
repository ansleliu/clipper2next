# clipper2next Qt Quick Demo

This optional demo uses Qt Quick/QML for the UI and a C++ controller for
clipper2next operations and timing.

## Layout

- Canvas on the left/center for subject, clip, result, and RectClip boundary.
- Inspector on the right for operation, input, and performance parameters.
- Command bar at the top for scene selection and high-frequency actions.
- Metrics bar at the bottom for algorithm time, render time, contour count,
  point count, area, and seed.

Algorithm timing is measured in C++ around the clipper2next call only. Rendering
time is measured by the QML canvas separately.

## Configure

The demo is disabled by default and requires Qt only when requested:

```powershell
cmake --preset msvc-qml-demo
cmake --build --preset msvc-qml-demo --target clipper2next_qml_demo
```

The preset enables the `qml-demo` vcpkg feature, which adds Qt dependencies.

## Default Build

Normal library, test, and benchmark builds do not require Qt:

```powershell
cmake --preset msvc-product
cmake --build --preset msvc-product --target clipper2next_tests
```
