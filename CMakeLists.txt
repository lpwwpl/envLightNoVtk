cmake_minimum_required(VERSION 3.21)

project(envLight VERSION 2026.08.3 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# Prefer Qt 6, but allow Qt 5.15 projects to build from the same tree.
find_package(Qt6 QUIET COMPONENTS Widgets OpenGL OpenGLWidgets)
if(Qt6_FOUND)
    set(QT_PACKAGE Qt6)
    set(QT_LIBS Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets)
else()
    find_package(Qt5 5.15 REQUIRED COMPONENTS Widgets OpenGL)
    set(QT_PACKAGE Qt5)
    set(QT_LIBS Qt5::Widgets Qt5::OpenGL)
endif()


set(ENVLIGHT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/envLight)

set(ENVLIGHT_SOURCES
    ${ENVLIGHT_DIR}/main.cpp
    ${ENVLIGHT_DIR}/mainwindow.cpp
    ${ENVLIGHT_DIR}/StandardSkyViewer.cpp
    ${ENVLIGHT_DIR}/OpenGLSceneWidget.cpp
    ${ENVLIGHT_DIR}/panorama_processor.cpp
    ${ENVLIGHT_DIR}/CIEWidget.cpp
    ${ENVLIGHT_DIR}/SkyPolarWidget.cpp
    ${ENVLIGHT_DIR}/SkyPerspectiveWidget.cpp
    ${ENVLIGHT_DIR}/SunSky.cpp
    ${ENVLIGHT_DIR}/EpwReader.cpp
    ${ENVLIGHT_DIR}/WeatherEffects.cpp
	    ${ENVLIGHT_DIR}/SkySceneWidget.cpp
    ${ENVLIGHT_DIR}/environment_light.cpp
)

set(ENVLIGHT_HEADERS
    ${ENVLIGHT_DIR}/mainwindow.h
		    ${ENVLIGHT_DIR}/SkySceneWidget.h
    ${ENVLIGHT_DIR}/StandardSkyViewer.h
    ${ENVLIGHT_DIR}/OpenGLSceneWidget.h
    ${ENVLIGHT_DIR}/CameraTransform.h
    ${ENVLIGHT_DIR}/panorama_processor.h
    ${ENVLIGHT_DIR}/CIEWidget.h
    ${ENVLIGHT_DIR}/SkyPolarWidget.h
    ${ENVLIGHT_DIR}/SkyPerspectiveWidget.h
    ${ENVLIGHT_DIR}/SunSky.hpp
    ${ENVLIGHT_DIR}/EpwReader.h
    ${ENVLIGHT_DIR}/EpwData.hpp
    ${ENVLIGHT_DIR}/WeatherEffects.h
    ${ENVLIGHT_DIR}/environment_light.h
    ${ENVLIGHT_DIR}/VL234f.hpp
    ${ENVLIGHT_DIR}/stb_image.h
    ${ENVLIGHT_DIR}/stb_image_write.h
    ${ENVLIGHT_DIR}/tinyexr.h
)

add_executable(envLight
    ${ENVLIGHT_SOURCES}
    ${ENVLIGHT_HEADERS}
)

target_include_directories(envLight PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/envLight
    ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty
	${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/tinyexr-3.0.0
	${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/tinyexr-3.0.0/deps/miniz
)
target_compile_definitions(envLight PRIVATE
	TINYEXR_USE_PIZ
	_CRT_SECURE_NO_WARNINGS
	STB_IMAGE_WRITE_IMPLEMENTATION
	STB_IMAGE_IMPLEMENTATION
    _WINDOWS
    UNICODE
    _UNICODE
    WIN32
    WIN64
)
# ---- 编译选项 ----
target_compile_options(envLight PRIVATE
    /openmp
    /utf-8
    /wd4577
    /wd4467
)
set(LIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/x64/Release")
set(MINIZ_LIB          ${LIB_DIR}/miniz.lib)
target_link_libraries(envLight PRIVATE
    ${QT_LIBS}
	${MINIZ_LIB}
)

if(MSVC)
    target_compile_options(envLight PRIVATE /W4 /permissive- /utf-8)
    target_compile_definitions(envLight PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
else()
    target_compile_options(envLight PRIVATE -Wall -Wextra -Wpedantic)
endif()

# VTK is intentionally not required. The old vtk_scene.* and scene3dwidget.*
# files may remain in the source tree for reference, but are not compiled.
# 安装 miniz 头文件
if(EXISTS "${THIRDPARTY_DIR}/tinyexr-3.0.0/")
    install(DIRECTORY "${THIRDPARTY_DIR}/tinyexr-3.0.0/"
        DESTINATION include/tinyexr-3.0.0
        FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp" PATTERN "*.inl"
    )
endif()

install(FILES ${MINIZ_LIB} DESTINATION lib)
message(STATUS "envLight renderer: Qt QOpenGLWidget + OpenGL 3.3 Core (no VTK)")
message(STATUS "Qt package: ${QT_PACKAGE}")
