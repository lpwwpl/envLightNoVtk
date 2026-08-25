#include "SkyPerspectiveWidget.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QTimer>
#include <QVector>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kDegToRad = kPi / 180.0;

SSLib::Vec3f toSunSky(const QVector3D& value)
{
    return {value.x(), value.y(), value.z()};
}

} // namespace

SkyPerspectiveWidget::SkyPerspectiveWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(520, 340);
    setMouseTracking(true);

    m_weatherTimer = new QTimer(this);
    m_weatherTimer->setInterval(33); // approximately 30 FPS
    connect(
        m_weatherTimer,
        &QTimer::timeout,
        this,
        &SkyPerspectiveWidget::advanceWeatherAnimation);
}

void SkyPerspectiveWidget::setParameters(
    const SkyPerspectiveParameters& parameters)
{
    m_parameters = parameters;

    m_parameters.cieSkyType =
        std::max(0, std::min(14, m_parameters.cieSkyType));

    while (m_parameters.cameraAzimuthDeg < 0.0)
        m_parameters.cameraAzimuthDeg += 360.0;
    while (m_parameters.cameraAzimuthDeg >= 360.0)
        m_parameters.cameraAzimuthDeg -= 360.0;

    m_parameters.cameraPitchDeg =
        clamp(m_parameters.cameraPitchDeg, -89.0, 89.0);
    m_parameters.verticalFovDeg =
        clamp(m_parameters.verticalFovDeg, 10.0, 170.0);

    m_parameters.targetValue =
        std::max(0.0, m_parameters.targetValue);
    m_parameters.directNormalValue =
        std::max(0.0, m_parameters.directNormalValue);
    m_parameters.displayReferenceValue =
        std::max(1.0e-9, m_parameters.displayReferenceValue);
    m_parameters.exposure =
        std::max(0.001, m_parameters.exposure);
    m_parameters.gamma =
        std::max(0.1, m_parameters.gamma);
    m_parameters.sunAngularRadiusDeg =
        clamp(m_parameters.sunAngularRadiusDeg, 0.05, 5.0);

    if (m_parameters.sunDirection.lengthSquared() < 1.0e-12f)
        m_parameters.sunDirection = QVector3D(0.0f, 0.0f, 1.0f);

    m_parameters.sunDirection.normalize();
    m_parameters.weather.intensity =
        clamp(m_parameters.weather.intensity, 0.0, 1.0);
    m_parameters.weather.fogDensity =
        clamp(m_parameters.weather.fogDensity, 0.0, 1.0);

    if (m_parameters.animateWeather && weatherNeedsAnimation()) {
        if (!m_weatherTimer->isActive())
            m_weatherTimer->start();
    } else {
        m_weatherTimer->stop();
    }

    rebuildPreview();
    update();
}

const SkyPerspectiveParameters&
SkyPerspectiveWidget::parameters() const
{
    return m_parameters;
}

double SkyPerspectiveWidget::clamp(
    double value, double low, double high)
{
    return std::max(low, std::min(value, high));
}

double SkyPerspectiveWidget::hash01(int index, int salt)
{
    const double value =
        std::sin(
            index * 12.9898
            + salt * 78.233)
        * 43758.5453123;
    return value - std::floor(value);
}

QVector3D SkyPerspectiveWidget::mix(
    const QVector3D& a,
    const QVector3D& b,
    double t)
{
    const float tf = static_cast<float>(clamp(t, 0.0, 1.0));
    return a * (1.0f - tf) + b * tf;
}

double SkyPerspectiveWidget::relativeSkyValue(
    const QVector3D& direction) const
{
    if (direction.z() <= 0.0f)
        return 0.0;

    const SSLib::Vec3f sky =
        toSunSky(direction.normalized());
    const SSLib::Vec3f sun =
        toSunSky(m_parameters.sunDirection.normalized());

    double value = 0.0;

    if (m_parameters.customCoefficients) {
        value = SSLib::CIECustomSky(
            m_parameters.coefficients,
            sky,
            sun,
            1.0f);
    } else {
        value = SSLib::CIEStandardSky(
            m_parameters.cieSkyType,
            sky,
            sun,
            1.0f);
    }

    if (!std::isfinite(value))
        return 0.0;

    return std::max(0.0, value);
}

double SkyPerspectiveWidget::absoluteScale() const
{
    if (m_parameters.targetValue <= 0.0)
        return 0.0;

    if (m_parameters.scaleMode ==
        SkyAbsoluteScaleMode::ZenithLuminance) {

        const double zenithRelative =
            relativeSkyValue(
                QVector3D(0.0f, 0.0f, 1.0f));

        return zenithRelative > 1.0e-12
            ? m_parameters.targetValue / zenithRelative
            : 0.0;
    }

    // Horizontal diffuse quantity:
    // E_h = integral_hemisphere L(omega) cos(theta) dOmega.
    const int altitudeSteps = 72;
    const int azimuthSteps = 288;

    const double dAltitude =
        (0.5 * kPi) / altitudeSteps;
    const double dAzimuth =
        (2.0 * kPi) / azimuthSteps;

    double integral = 0.0;

    for (int altitudeIndex = 0;
         altitudeIndex < altitudeSteps;
         ++altitudeIndex) {

        const double altitude =
            (altitudeIndex + 0.5) * dAltitude;
        const double sinAltitude =
            std::sin(altitude);
        const double cosAltitude =
            std::cos(altitude);

        for (int azimuthIndex = 0;
             azimuthIndex < azimuthSteps;
             ++azimuthIndex) {

            const double azimuth =
                (azimuthIndex + 0.5) * dAzimuth;

            const QVector3D direction(
                static_cast<float>(
                    cosAltitude * std::sin(azimuth)),
                static_cast<float>(
                    cosAltitude * std::cos(azimuth)),
                static_cast<float>(sinAltitude));

            integral +=
                relativeSkyValue(direction)
                * sinAltitude
                * cosAltitude
                * dAltitude
                * dAzimuth;
        }
    }

    return integral > 1.0e-12
        ? m_parameters.targetValue / integral
        : 0.0;
}

double SkyPerspectiveWidget::toneMappedValue(
    double value,
    double referenceValue) const
{
    if (value <= 0.0 || referenceValue <= 0.0)
        return 0.0;

    const double mapped =
        1.0 - std::exp(
            -m_parameters.exposure
            * value
            / referenceValue);

    return clamp(
        std::pow(
            clamp(mapped, 0.0, 1.0),
            1.0 / m_parameters.gamma),
        0.0,
        1.0);
}

double SkyPerspectiveWidget::clearSkyFactor() const
{
    if (m_parameters.customCoefficients)
        return 0.55;

    const int type = m_parameters.cieSkyType + 1;

    if (type <= 4)
        return 0.08;
    if (type == 5)
        return 0.22;
    if (type <= 8)
        return 0.48;
    if (type <= 11)
        return 0.68;

    return 0.95;
}

double SkyPerspectiveWidget::atmosphericAttenuation() const
{
    const double precipitation =
        m_parameters.weather.precipitation == PrecipitationKind::None
        ? 0.0
        : m_parameters.weather.intensity;
    const double fog = m_parameters.weather.fogDensity;
    return std::exp(-1.5 * precipitation - 2.2 * fog);
}

QColor SkyPerspectiveWidget::applyWeatherAtmosphere(
    const QColor& source) const
{
    const double precipitation =
        m_parameters.weather.precipitation == PrecipitationKind::None
        ? 0.0
        : m_parameters.weather.intensity;
    const double fog = m_parameters.weather.fogDensity;
    const double cloud = clamp(
        m_parameters.weather.opaqueSkyCoverTenths / 10.0,
        0.0,
        1.0);

    double blend = clamp(
        0.16 * precipitation
        + 0.72 * fog
        + 0.10 * cloud,
        0.0,
        0.88);

    QColor atmosphere(183, 190, 197);
    if (m_parameters.weather.precipitation == PrecipitationKind::Snow)
        atmosphere = QColor(208, 214, 220);

    return QColor::fromRgbF(
        source.redF() * (1.0 - blend) + atmosphere.redF() * blend,
        source.greenF() * (1.0 - blend) + atmosphere.greenF() * blend,
        source.blueF() * (1.0 - blend) + atmosphere.blueF() * blend,
        source.alphaF());
}

QColor SkyPerspectiveWidget::groundColor() const
{
    if (!m_parameters.showWeatherGround)
        return QColor(35, 37, 40);

    const bool fallingSnow =
        m_parameters.weather.precipitation == PrecipitationKind::Snow
        || m_parameters.weather.precipitation == PrecipitationKind::Mixed;

    if (m_parameters.weather.snowDepthCm > 0.0 || fallingSnow) {
        const double coverage = clamp(
            std::max(
                m_parameters.weather.snowDepthCm / 5.0,
                fallingSnow ? 0.45 * m_parameters.weather.intensity : 0.0),
            0.0,
            1.0);
        const double albedo = clamp(
            m_parameters.weather.groundAlbedo,
            0.2,
            0.95);
        const int value = static_cast<int>(
            80.0 + 165.0 * coverage * albedo);
        return QColor(value, value, std::min(255, value + 5));
    }

    if (m_parameters.weather.precipitation == PrecipitationKind::Rain
        || m_parameters.weather.precipitation == PrecipitationKind::Mixed
        || m_parameters.weather.precipitation == PrecipitationKind::FreezingRain) {
        return QColor(22, 27, 31);
    }

    return QColor(35, 37, 40);
}

bool SkyPerspectiveWidget::weatherNeedsAnimation() const
{
    if (!m_parameters.showWeatherParticles)
        return false;

    return m_parameters.weather.intensity > 0.0
        && m_parameters.weather.precipitation != PrecipitationKind::None;
}

QVector3D SkyPerspectiveWidget::cameraRay(
    int x,
    int y,
    int width,
    int height) const
{
    const double yaw =
        m_parameters.cameraAzimuthDeg * kDegToRad;
    const double pitch =
        m_parameters.cameraPitchDeg * kDegToRad;

    // ENU coordinates:
    // +X East, +Y North, +Z Zenith.
    const QVector3D forward(
        static_cast<float>(
            std::cos(pitch) * std::sin(yaw)),
        static_cast<float>(
            std::cos(pitch) * std::cos(yaw)),
        static_cast<float>(std::sin(pitch)));

    QVector3D right =
        QVector3D::crossProduct(
            forward,
            QVector3D(0.0f, 0.0f, 1.0f));

    if (right.lengthSquared() < 1.0e-10f)
        right = QVector3D(1.0f, 0.0f, 0.0f);
    else
        right.normalize();

    const QVector3D up =
        QVector3D::crossProduct(
            right,
            forward).normalized();

    const double aspect =
        static_cast<double>(width)
        / std::max(1, height);

    const double tanHalfFov =
        std::tan(
            0.5
            * m_parameters.verticalFovDeg
            * kDegToRad);

    const double screenX =
        (2.0 * (x + 0.5) / width - 1.0)
        * aspect
        * tanHalfFov;

    const double screenY =
        (1.0 - 2.0 * (y + 0.5) / height)
        * tanHalfFov;

    return (
        forward
        + static_cast<float>(screenX) * right
        + static_cast<float>(screenY) * up
    ).normalized();
}

QColor SkyPerspectiveWidget::falseColor(double value)
{
    const double t = clamp(value, 0.0, 1.0);

    const double r =
        clamp(
            1.5 - std::abs(4.0 * t - 3.0),
            0.0,
            1.0);

    const double g =
        clamp(
            1.5 - std::abs(4.0 * t - 2.0),
            0.0,
            1.0);

    const double b =
        clamp(
            1.5 - std::abs(4.0 * t - 1.0),
            0.0,
            1.0);

    return QColor::fromRgbF(r, g, b);
}

QColor SkyPerspectiveWidget::naturalPreviewColor(
    const QVector3D& direction,
    double normalizedBrightness,
    double sunCosine,
    double directStrength) const
{
    const double clarity = clearSkyFactor();
    const double altitudeFactor =
        std::pow(
            clamp(
                static_cast<double>(direction.z()),
                0.0,
                1.0),
            0.35);

    const QVector3D overcastHorizon(
        0.70f, 0.72f, 0.74f);
    const QVector3D overcastZenith(
        0.55f, 0.59f, 0.63f);

    const QVector3D clearHorizon(
        0.68f, 0.82f, 0.98f);
    const QVector3D clearZenith(
        0.10f, 0.30f, 0.76f);

    const QVector3D horizonColor =
        mix(overcastHorizon, clearHorizon, clarity);
    const QVector3D zenithColor =
        mix(overcastZenith, clearZenith, clarity);

    QVector3D rgb =
        mix(
            horizonColor,
            zenithColor,
            altitudeFactor);

    if (m_parameters.showSunGlow &&
        m_parameters.sunDirection.z() > 0.0f) {

        const double sunAngle =
            std::acos(
                clamp(sunCosine, -1.0, 1.0));

        // Display-only circumsolar whitening.
        const double glowWidth =
            (5.0 + 16.0 * (1.0 - clarity))
            * kDegToRad;

        const double glow =
            std::exp(
                -sunAngle
                / std::max(1.0e-6, glowWidth))
            * directStrength
            * (0.25 + 0.75 * clarity);

        const QVector3D warmWhite(
            1.0f, 0.92f, 0.72f);

        rgb = mix(
            rgb,
            warmWhite,
            clamp(glow * 0.65, 0.0, 0.75));
    }

    // The CIE model controls luminance. The RGB hue above is only
    // a natural-looking preview and is not a spectral simulation.
    const double brightness =
        clamp(
            normalizedBrightness * 1.20,
            0.0,
            1.0);

    rgb *= static_cast<float>(brightness);

    return QColor::fromRgbF(
        clamp(rgb.x(), 0.0, 1.0),
        clamp(rgb.y(), 0.0, 1.0),
        clamp(rgb.z(), 0.0, 1.0));
}

QImage SkyPerspectiveWidget::renderBaseImage(
    const QSize& imageSize) const
{
    const int width =
        std::max(1, imageSize.width());
    const int height =
        std::max(1, imageSize.height());

    QImage image(
        width,
        height,
        QImage::Format_ARGB32);

    image.fill(QColor(28, 30, 34));

    const double scale = absoluteScale();

    QVector<double> samples(
        width * height,
        0.0);

    double skyPeak = 0.0;

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const QVector3D direction =
                cameraRay(
                    x, y, width, height);

            if (direction.z() <= 0.0f)
                continue;

            const double value =
                relativeSkyValue(direction) * scale;

            samples[y * width + x] = value;
            skyPeak = std::max(skyPeak, value);
        }
    }

    const double referenceValue =
        m_parameters.toneMapMode ==
            SkyToneMapMode::AutoPeak
        ? std::max(1.0e-9, skyPeak)
        : std::max(
            1.0e-9,
            m_parameters.displayReferenceValue);

    const QVector3D sun =
        m_parameters.sunDirection.normalized();

    const double sunRadiusRadians =
        m_parameters.sunAngularRadiusDeg
        * kDegToRad;

    const double cosSunRadius =
        std::cos(sunRadiusRadians);

    const double sunSolidAngle =
        2.0 * kPi
        * (1.0 - std::cos(sunRadiusRadians));

    const double effectiveDirectNormal =
        m_parameters.directNormalValue
        * atmosphericAttenuation();

    const double directDiskValue =
        sunSolidAngle > 1.0e-12
        ? effectiveDirectNormal / sunSolidAngle
        : 0.0;

    const double directStrength =
        clamp(
            effectiveDirectNormal / 800.0,
            0.0,
            1.0);

    for (int y = 0; y < height; ++y) {
        QRgb* scanline =
            reinterpret_cast<QRgb*>(
                image.scanLine(y));

        for (int x = 0; x < width; ++x) {
            const QVector3D direction =
                cameraRay(
                    x, y, width, height);

            if (direction.z() <= 0.0f) {
                scanline[x] = groundColor().rgba();
                continue;
            }

            const double value =
                samples[y * width + x];

            const double normalized =
                toneMappedValue(
                    value,
                    referenceValue);

            const double sunCosine =
                QVector3D::dotProduct(
                    direction,
                    sun);

            QColor color;

            switch (m_parameters.colorMode) {
            case SkyColorMode::FalseColor:
                color = falseColor(normalized);
                break;

            case SkyColorMode::NaturalPreview:
                color = naturalPreviewColor(
                    direction,
                    normalized,
                    sunCosine,
                    directStrength);
                break;

            case SkyColorMode::GrayscaleLuminance:
            default:
                color = QColor::fromRgbF(
                    normalized,
                    normalized,
                    normalized);
                break;
            }

            color = applyWeatherAtmosphere(color);

            if (m_parameters.showSunDisk &&
                effectiveDirectNormal > 0.0 &&
                sun.z() > 0.0f &&
                sunCosine >= cosSunRadius) {

                const double sunNormalized =
                    toneMappedValue(
                        directDiskValue,
                        referenceValue);

                const double warm =
                    clamp(
                        0.75 + 0.25 * sunNormalized,
                        0.0,
                        1.0);

                color = QColor::fromRgbF(
                    warm,
                    warm * 0.97,
                    warm * 0.86);
            }

            scanline[x] = color.rgba();
        }
    }

    return image;
}

void SkyPerspectiveWidget::drawWeatherOverlay(
    QPainter& painter,
    const QRectF& targetRect,
    double animationSeconds) const
{
    if (!m_parameters.showWeatherParticles
        || m_parameters.weather.intensity <= 0.0)
        return;

    const PrecipitationKind kind =
        m_parameters.weather.precipitation;

    if (kind == PrecipitationKind::None)
        return;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setClipRect(targetRect);

    const double intensity = m_parameters.weather.intensity;
    const double width = targetRect.width();
    const double height = targetRect.height();

    // EPW direction is the direction FROM which wind blows. The horizontal
    // destination direction is therefore rotated by 180 degrees.
    const double relativeWind =
        (m_parameters.weather.windDirectionDeg
         + 180.0
         - m_parameters.cameraAzimuthDeg)
        * kDegToRad;

    const double windSpeed = m_parameters.weather.windSpeedMps;
    const double screenWind =
        std::sin(relativeWind)
        * clamp(windSpeed / 12.0, 0.0, 1.8);

    auto wrapped = [](double value) {
        value -= std::floor(value);
        return value;
    };

    const bool drawRain =
        kind == PrecipitationKind::Rain
        || kind == PrecipitationKind::Mixed
        || kind == PrecipitationKind::FreezingRain;

    const bool drawSnow =
        kind == PrecipitationKind::Snow
        || kind == PrecipitationKind::Mixed;

    const bool drawHail = kind == PrecipitationKind::Hail;

    if (drawRain) {
        const int count = static_cast<int>(80 + 620 * intensity);
        const double speed = 0.55 + 1.25 * intensity;

        QPen pen(QColor(205, 224, 238, static_cast<int>(80 + 110 * intensity)));
        pen.setWidthF(0.7 + 1.0 * intensity);
        pen.setCapStyle(Qt::RoundCap);
        painter.setPen(pen);

        for (int i = 0; i < count; ++i) {
            const double seedX = hash01(i, 1);
            const double seedY = hash01(i, 2);
            const double speedScale = 0.75 + 0.55 * hash01(i, 3);
            const double y = wrapped(seedY + animationSeconds * speed * speedScale);
            const double x = wrapped(
                seedX
                + animationSeconds * screenWind * 0.035
                + y * screenWind * 0.055);

            const double px = targetRect.left() + x * width;
            const double py = targetRect.top() + y * height;
            const double length = (8.0 + 22.0 * intensity) * speedScale;
            const double dx = screenWind * length * 0.75;

            painter.drawLine(
                QPointF(px - dx, py - length),
                QPointF(px, py));
        }
    }

    if (drawSnow) {
        const int count = static_cast<int>(45 + 360 * intensity);
        const double speed = 0.10 + 0.28 * intensity;

        painter.setPen(Qt::NoPen);

        for (int i = 0; i < count; ++i) {
            const double seedX = hash01(i, 11);
            const double seedY = hash01(i, 12);
            const double seedSize = hash01(i, 13);
            const double seedPhase = hash01(i, 14) * 2.0 * kPi;
            const double y = wrapped(seedY + animationSeconds * speed * (0.7 + seedSize));
            const double flutter =
                std::sin(animationSeconds * (0.7 + seedSize) + seedPhase)
                * (0.008 + 0.018 * seedSize);
            const double x = wrapped(
                seedX
                + animationSeconds * screenWind * 0.014
                + flutter);

            const double px = targetRect.left() + x * width;
            const double py = targetRect.top() + y * height;
            const double radius = 1.1 + 3.4 * seedSize;
            const int alpha = static_cast<int>(120 + 110 * seedSize);

            painter.setBrush(QColor(248, 251, 255, alpha));
            painter.drawEllipse(QPointF(px, py), radius, radius);
        }
    }

    if (drawHail) {
        const int count = static_cast<int>(40 + 260 * intensity);
        painter.setPen(QPen(QColor(230, 239, 247, 210), 0.8));
        painter.setBrush(QColor(221, 234, 244, 190));

        for (int i = 0; i < count; ++i) {
            const double seedX = hash01(i, 21);
            const double seedY = hash01(i, 22);
            const double y = wrapped(seedY + animationSeconds * (0.8 + intensity));
            const double x = wrapped(seedX + animationSeconds * screenWind * 0.025);
            const double radius = 1.3 + 2.6 * hash01(i, 23);
            painter.drawEllipse(
                QPointF(
                    targetRect.left() + x * width,
                    targetRect.top() + y * height),
                radius,
                radius);
        }
    }

    painter.restore();
}

QImage SkyPerspectiveWidget::renderToImage(
    const QSize& imageSize) const
{
    QImage image = renderBaseImage(imageSize);
    QPainter painter(&image);
    drawWeatherOverlay(
        painter,
        QRectF(0.0, 0.0, image.width(), image.height()),
        m_animationSeconds);
    return image;
}

bool SkyPerspectiveWidget::savePng(
    const QString& filePath,
    const QSize& imageSize) const
{
    return renderToImage(imageSize)
        .save(filePath, "PNG");
}

void SkyPerspectiveWidget::rebuildPreview()
{
    if (width() <= 0 || height() <= 0)
        return;

    // Keep mouse interaction responsive. Export can use any resolution.
    const QSize previewSize =
        size().boundedTo(QSize(800, 520));

    m_preview =
        renderBaseImage(previewSize);
}

void SkyPerspectiveWidget::paintEvent(
    QPaintEvent*)
{
    QPainter painter(this);
    painter.fillRect(
        rect(),
        QColor(28, 30, 34));

    if (m_preview.isNull())
        rebuildPreview();

    if (!m_preview.isNull())
        painter.drawImage(rect(), m_preview);

    drawWeatherOverlay(
        painter,
        QRectF(rect()),
        m_animationSeconds);

    painter.setRenderHint(
        QPainter::Antialiasing,
        true);

    if (m_parameters.showHorizon) {
        QPainterPath horizonPath;
        bool started = false;

        const int samples =
            std::max(64, width());

        for (int sample = 0;
             sample < samples;
             ++sample) {

            const int x =
                static_cast<int>(
                    sample
                    * (width() - 1.0)
                    / std::max(1, samples - 1));

            int bestY = -1;
            double bestAbsoluteZ =
                std::numeric_limits<double>::max();

            for (int y = 0;
                 y < height();
                 y += 2) {

                const double absoluteZ =
                    std::abs(
                        cameraRay(
                            x,
                            y,
                            width(),
                            height()).z());

                if (absoluteZ < bestAbsoluteZ) {
                    bestAbsoluteZ = absoluteZ;
                    bestY = y;
                }
            }

            if (bestY >= 0 &&
                bestAbsoluteZ < 0.05) {

                if (!started) {
                    horizonPath.moveTo(x, bestY);
                    started = true;
                } else {
                    horizonPath.lineTo(x, bestY);
                }
            }
        }

        painter.setPen(
            QPen(
                QColor(255, 255, 255, 150),
                1.0));

        painter.drawPath(horizonPath);
    }

    painter.setPen(
        QColor(255, 255, 255, 220));

    painter.drawText(
        12,
        22,
        QString(
            "View Az %1°  View Alt %2°  VFOV %3°")
            .arg(
                m_parameters.cameraAzimuthDeg,
                0,
                'f',
                1)
            .arg(
                m_parameters.cameraPitchDeg,
                0,
                'f',
                1)
            .arg(
                m_parameters.verticalFovDeg,
                0,
                'f',
                1));

    if (!m_parameters.weather.description.isEmpty()) {
        painter.drawText(
            12,
            43,
            QString("Weather: %1")
                .arg(m_parameters.weather.description));
    }
}

void SkyPerspectiveWidget::advanceWeatherAnimation()
{
    m_animationSeconds += 0.033;
    if (m_animationSeconds > 10000.0)
        m_animationSeconds = 0.0;
    update();
}

void SkyPerspectiveWidget::resizeEvent(
    QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    rebuildPreview();
}

void SkyPerspectiveWidget::mousePressEvent(
    QMouseEvent* event)
{
    m_lastMousePosition = event->pos();
    event->accept();
}

void SkyPerspectiveWidget::mouseMoveEvent(
    QMouseEvent* event)
{
    if (!(event->buttons() & Qt::LeftButton))
        return;

    const QPoint delta =
        event->pos() - m_lastMousePosition;

    m_lastMousePosition =
        event->pos();

    m_parameters.cameraAzimuthDeg -=
        delta.x() * 0.25;

    while (
        m_parameters.cameraAzimuthDeg < 0.0)
        m_parameters.cameraAzimuthDeg += 360.0;

    while (
        m_parameters.cameraAzimuthDeg >= 360.0)
        m_parameters.cameraAzimuthDeg -= 360.0;

    m_parameters.cameraPitchDeg =
        clamp(
            m_parameters.cameraPitchDeg
                + delta.y() * 0.20,
            -89.0,
            89.0);

    rebuildPreview();
    update();

    emit cameraChanged(
        m_parameters.cameraAzimuthDeg,
        m_parameters.cameraPitchDeg,
        m_parameters.verticalFovDeg);
}

void SkyPerspectiveWidget::wheelEvent(
    QWheelEvent* event)
{
    const double steps =
        event->angleDelta().y() / 120.0;

    m_parameters.verticalFovDeg =
        clamp(
            m_parameters.verticalFovDeg
                - steps * 5.0,
            10.0,
            170.0);

    rebuildPreview();
    update();

    emit cameraChanged(
        m_parameters.cameraAzimuthDeg,
        m_parameters.cameraPitchDeg,
        m_parameters.verticalFovDeg);

    event->accept();
}
