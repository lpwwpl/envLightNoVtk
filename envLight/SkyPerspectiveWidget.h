#ifndef SKYPERSPECTIVEWIDGET_H
#define SKYPERSPECTIVEWIDGET_H

#include <QColor>
#include <QImage>
#include <QPoint>
#include <QVector3D>
#include <QWidget>

#include "SunSky.hpp"
#include "WeatherEffects.h"

class QPainter;
class QTimer;

enum class SkyAbsoluteScaleMode {
    ZenithLuminance,              // cd/m2
    DiffuseHorizontalIlluminance, // lx
    DiffuseHorizontalIrradiance   // W/m2
};

enum class SkyColorMode {
    GrayscaleLuminance,
    FalseColor,
    NaturalPreview
};

enum class SkyToneMapMode {
    FixedReference,
    AutoPeak
};

struct SkyPerspectiveParameters {
    int cieSkyType = 11; // 0-based: CIE type 12
    bool customCoefficients = false;
    SSLib::CIESkyCoefficients coefficients;

    SkyAbsoluteScaleMode scaleMode =
        SkyAbsoluteScaleMode::DiffuseHorizontalIrradiance;
    double targetValue = 100.0;

    // W/m2 in irradiance mode, lx in photometric modes.
    double directNormalValue = 600.0;

    QVector3D sunDirection{0.5f, -0.5f, 0.7071f};

    // Camera: azimuth clockwise from North; pitch above horizon.
    double cameraAzimuthDeg = 180.0;
    double cameraPitchDeg = 20.0;
    double verticalFovDeg = 90.0;

    SkyColorMode colorMode = SkyColorMode::NaturalPreview;
    SkyToneMapMode toneMapMode = SkyToneMapMode::FixedReference;
    double displayReferenceValue = 50.0;
    double exposure = 1.0;
    double gamma = 2.2;

    bool showHorizon = true;
    bool showSunDisk = true;
    bool showSunGlow = true;
    double sunAngularRadiusDeg = 0.2665;

    // Weather is a visual layer driven by EPW. It does not change the CIE
    // mathematical sky distribution; it attenuates the preview and adds
    // rain/snow/fog/ground-snow cues on top of that distribution.
    WeatherVisualState weather;
    bool animateWeather = true;
    bool showWeatherParticles = true;
    bool showWeatherGround = true;
};

class SkyPerspectiveWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit SkyPerspectiveWidget(QWidget* parent = nullptr);

    void setParameters(const SkyPerspectiveParameters& parameters);
    const SkyPerspectiveParameters& parameters() const;

    QImage renderToImage(const QSize& imageSize) const;
    bool savePng(const QString& filePath, const QSize& imageSize) const;

signals:
    void cameraChanged(
        double azimuthDeg,
        double pitchDeg,
        double verticalFovDeg);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private slots:
    void advanceWeatherAnimation();

private:
    void rebuildPreview();

    QImage renderBaseImage(const QSize& imageSize) const;
    void drawWeatherOverlay(
        QPainter& painter,
        const QRectF& targetRect,
        double animationSeconds) const;

    double relativeSkyValue(const QVector3D& direction) const;
    double absoluteScale() const;
    double toneMappedValue(double value, double referenceValue) const;
    double clearSkyFactor() const;
    double atmosphericAttenuation() const;

    QVector3D cameraRay(
        int x, int y, int width, int height) const;

    QColor naturalPreviewColor(
        const QVector3D& direction,
        double normalizedBrightness,
        double sunCosine,
        double directStrength) const;

    QColor applyWeatherAtmosphere(const QColor& color) const;
    QColor groundColor() const;
    bool weatherNeedsAnimation() const;

    static QColor falseColor(double normalized);
    static double clamp(double value, double low, double high);
    static double hash01(int index, int salt);
    static QVector3D mix(
        const QVector3D& a,
        const QVector3D& b,
        double t);

private:
    SkyPerspectiveParameters m_parameters;
    QImage m_preview;
    QPoint m_lastMousePosition;
    QTimer* m_weatherTimer = nullptr;
    double m_animationSeconds = 0.0;
};

#endif // SKYPERSPECTIVEWIDGET_H
