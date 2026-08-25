# envLight - OpenGL replacement for VTK

This project is based on the latest Camera-local/ENU/North/Vertical-Flip version and replaces the VTK 3D diagnostic widget with `OpenGLSceneWidget` implemented using Qt `QOpenGLWidget` + OpenGL 3.3 Core.

## Coordinate conventions

Camera local:
- +Xc: camera local X
- +Yc: camera local Y (historical image-Y convention is retained by `CameraTransform.h`)
- +Zc: Forward / optical axis

World/Panorama/OpenGL scene:
- +X: East
- +Y: North
- +Z: Up

The same `CameraTransform::RayContext` is used by perspective generation, panorama ROI, camera local translation, OpenGL frustum and camera axes.

## 3D widget features

- Textured ENU unit sphere using the current panorama
- Panorama north-position calibration (`northPanoramaDeg`)
- Fixed large ENU axes: E / N / U
- Small moving camera-local axes: Xc / Yc / Zc (Zc = Forward)
- Camera position marker
- Four perspective corner rays
- ROI quadrilateral on the sphere
- Mouse orbit + wheel zoom
- Same vertical-flip option used by perspective/ROI/frustum

## Build

Qt 6 recommended:

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/Qt/6.x.x/compiler
cmake --build build --config Release
```

Windows + Qt installed through the Qt Online Installer example:

```powershell
cmake -S . -B build -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64
cmake --build build --config Release
```

Qt 5.15 is also supported by the CMake file.

Dependencies:
- Qt Widgets
- Qt OpenGL / OpenGLWidgets
- system OpenGL
- zlib (TinyEXR is configured with `TINYEXR_USE_MINIZ=0`)

VTK is not required.

## Files changed

- Added `envLight/OpenGLSceneWidget.h`
- Added `envLight/OpenGLSceneWidget.cpp`
- `mainwindow.h/.cpp`: use `OpenGLSceneWidget` instead of `VTKSceneWidget`
- `main.cpp`: request OpenGL 3.3 Core format before creating QApplication
- `panorama_processor.cpp`: TinyEXR uses zlib rather than external miniz
- Added root `CMakeLists.txt`

The old `vtk_scene.*` files are left in the folder only as legacy reference and are excluded from CMake.
