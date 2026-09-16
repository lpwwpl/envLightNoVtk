#include "SkyPerspectiveWidget.h"
#include "SkySceneWidget.h"
#include "environment_light.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QString>
#include <QTimer>
#include <QVector>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kDegToRad = kPi / 180.0;
constexpr double kMaximumPhotopicEfficacy = 683.0;

// 功能：把 Qt 三维向量转换为 SunSky 使用的轻量 Vec3f。
SSLib::Vec3f toSunSky(const QVector3D& value)
{
    return SSLib::Vec3f(value.x(), value.y(), value.z());
}

// 功能：计算 CIE 1931 颜色匹配函数解析近似中使用的非对称高斯项。
double asymmetricGaussian(double wavelengthNm, double centerNm, double leftSlope, double rightSlope)
{
    const double slope = wavelengthNm < centerNm ? leftSlope : rightSlope;
    const double t = (wavelengthNm - centerNm) * slope;
    return std::exp(-0.5 * t * t);
}

// CIE 1931 XYZ 颜色匹配函数只用于把“已有的天空亮度”转换为颜色/光谱显示，
// 它与 CIE Standard General Sky 的 15 类天空分布不是同一个模型，不能混为一谈。
// 功能：返回 CIE 1931 2° x-bar 的解析近似值。
double cieXBar1931(double wavelengthNm)
{
    return 0.362 * asymmetricGaussian(wavelengthNm, 442.0, 0.0624, 0.0374) + 1.056 * asymmetricGaussian(wavelengthNm, 599.8, 0.0264, 0.0323) - 0.065 * asymmetricGaussian(wavelengthNm, 501.1, 0.0490, 0.0382);
}

// 功能：返回 CIE 1931 2° y-bar 的解析近似值。
double cieYBar1931(double wavelengthNm)
{
    return 0.821 * asymmetricGaussian(wavelengthNm, 568.8, 0.0213, 0.0247) + 0.286 * asymmetricGaussian(wavelengthNm, 530.9, 0.0613, 0.0322);
}

// 功能：返回 CIE 1931 2° z-bar 的解析近似值。
double cieZBar1931(double wavelengthNm)
{
    return 1.217 * asymmetricGaussian(wavelengthNm, 437.0, 0.0845, 0.0278) + 0.681 * asymmetricGaussian(wavelengthNm, 459.0, 0.0385, 0.0725);
}

// 功能：返回以 555 nm 为相对参考的 Planck 光谱形状，避免绝对黑体辐射数值过大。
double planckRelative(double wavelengthNm, double temperatureK)
{
    const double wavelength = std::max(1.0, wavelengthNm);
    const double temperature = std::max(1000.0, std::min(20000.0, temperatureK));
    constexpr double c2NmK = 1.438776877e7;
    constexpr double referenceNm = 555.0;

    const double numerator = std::pow(referenceNm / wavelength, 5.0) * std::expm1(c2NmK / (referenceNm * temperature));
    const double denominator = std::expm1(c2NmK / (wavelength * temperature));

    if (!std::isfinite(numerator) || !std::isfinite(denominator) || std::abs(denominator) < 1.0e-30) {
        return 0.0;
    }

    return std::max(0.0, numerator / denominator);
}

// ================================================================
// 光谱转换缓存
// ================================================================
struct SpectralConversion {
    double bandRadiancePerLuminance = 0.0;
    double spectralRadiancePerLuminance = 0.0;
    double colorimetricYPerLuminance = 1.0;
    QVector3D colorimetricRgb{1.0f, 1.0f, 1.0f};
    QVector3D wavelengthRgb{1.0f, 1.0f, 1.0f};
};

// 功能：把 XYZ 转换为线性 sRGB，并按最大分量归一化作为显示色度。
QVector3D normalizedLinearSrgbFromXyz(double x, double y, double z)
{
    double red = 3.2406 * x - 1.5372 * y - 0.4986 * z;
    double green = -0.9689 * x + 1.8758 * y + 0.0415 * z;
    double blue = 0.0557 * x - 0.2040 * y + 1.0570 * z;

    red = std::max(0.0, red);
    green = std::max(0.0, green);
    blue = std::max(0.0, blue);

    const double peak = std::max(red, std::max(green, blue));
    if (peak <= 1.0e-12)
        return QVector3D(1.0f, 1.0f, 1.0f);

    return QVector3D(static_cast<float>(red / peak), static_cast<float>(green / peak), static_cast<float>(blue / peak));
}

// 功能：根据光谱范围、CCT 与显示波长建立光度到辐射/色度的转换系数。
SpectralConversion buildSpectralConversion(const SkyPerspectiveParameters& parameters)
{
    SpectralConversion result;

    double fullVisibleY = 0.0;
    for (int wavelength = 360; wavelength <= 830; ++wavelength)
    {
        const double spectralPower = planckRelative(static_cast<double>(wavelength), parameters.spectralTemperatureK);
        fullVisibleY += spectralPower * cieYBar1931(static_cast<double>(wavelength));
    }

    const double photometricDenominator = kMaximumPhotopicEfficacy * std::max(1.0e-18, fullVisibleY);

    const double start = std::min(parameters.spectralStartNm, parameters.spectralEndNm);
    const double end = std::max(parameters.spectralStartNm, parameters.spectralEndNm);

    double bandPower = 0.0;
    double bandX = 0.0;
    double bandY = 0.0;
    double bandZ = 0.0;

    const int firstNm = static_cast<int>(std::floor(start));
    const int lastNm = static_cast<int>(std::ceil(end));

    for (int wavelengthIndex = firstNm; wavelengthIndex <= lastNm; ++wavelengthIndex) {
        const double wavelength = static_cast<double>(wavelengthIndex);
        if (wavelength < start || wavelength > end)
            continue;

        const double spectralPower = planckRelative(wavelength, parameters.spectralTemperatureK);
        bandPower += spectralPower;
        bandX += spectralPower * cieXBar1931(wavelength);
        bandY += spectralPower * cieYBar1931(wavelength);
        bandZ += spectralPower * cieZBar1931(wavelength);
    }

    result.bandRadiancePerLuminance = bandPower / photometricDenominator;

    const double selectedWavelength = std::max(start, std::min(end, parameters.displayWavelengthNm));
    result.spectralRadiancePerLuminance = planckRelative(selectedWavelength, parameters.spectralTemperatureK) / photometricDenominator;

    result.colorimetricYPerLuminance = bandY / std::max(1.0e-18, fullVisibleY);

    result.colorimetricRgb = normalizedLinearSrgbFromXyz(bandX / std::max(1.0e-18, fullVisibleY), bandY / std::max(1.0e-18, fullVisibleY), bandZ / std::max(1.0e-18, fullVisibleY));

    result.wavelengthRgb = normalizedLinearSrgbFromXyz(cieXBar1931(selectedWavelength), cieYBar1931(selectedWavelength), cieZBar1931(selectedWavelength));

    return result;
}

// 功能：把光度亮度转换为当前 Radiance Sensor 要求的标量结果。
double convertPhotometricValue(double luminance, const SkyPerspectiveParameters& parameters, const SpectralConversion& spectral)
{
    const double positiveLuminance = std::max(0.0, luminance);

    switch (parameters.measurementType) {
    case SkyMeasurementType::Photometric:
        return positiveLuminance;

    case SkyMeasurementType::Radiometric:
        return positiveLuminance * spectral.bandRadiancePerLuminance;

    case SkyMeasurementType::Colorimetric:
        return positiveLuminance * spectral.colorimetricYPerLuminance;

    case SkyMeasurementType::Spectral:
        if (parameters.spectralDisplayAllWavelengths)
            return positiveLuminance * spectral.colorimetricYPerLuminance;
        return positiveLuminance * spectral.spectralRadiancePerLuminance;
    }

    return positiveLuminance;
}

// 功能：把当前绝对标定域中的标量转换为传感器显示标量。
double convertSourceValue(double sourceValue, const SkyPerspectiveParameters& parameters, const SpectralConversion& spectral)
{
    // DHI 标定直接得到辐射量纲。CIEWidget 使用该模式时应保持 W/(m²·sr)
    // 标量，不应再次把它当 cd/m² 做 683 lm/W 光度转换。
    if (parameters.scaleMode == SkyAbsoluteScaleMode::DiffuseHorizontalIrradiance) {
        return std::max(0.0, sourceValue);
    }

    return convertPhotometricValue(sourceValue, parameters, spectral);
}

// 功能：按给定线性 RGB 色度和标量亮度生成 0~1 显示颜色。
QColor chromaticDisplayColor(const QVector3D& rgb, double brightness)
{
    const double red = std::max(0.0, std::min(1.0, static_cast<double>(rgb.x()) * brightness));
    const double green = std::max(0.0, std::min(1.0, static_cast<double>(rgb.y()) * brightness));
    const double blue = std::max(0.0, std::min(1.0, static_cast<double>(rgb.z()) * brightness));

    return QColor::fromRgbF(red, green, blue);
}

// 功能：把任意实数周期包裹到 [0, 1) 区间。
double wrapUnit(double value)
{
    value -= std::floor(value);
    return value;
}

// 功能：返回状态文字使用的传感器结果类型名称。
QString measurementTypeText(SkyMeasurementType type)
{
    switch (type) {
    case SkyMeasurementType::Photometric:
        return QString::fromUtf8("Photometric [cd/m²]");
    case SkyMeasurementType::Radiometric:
        return QString::fromUtf8("Radiometric [W/(m²·sr)]");
    case SkyMeasurementType::Colorimetric:
        return QString::fromUtf8("Colorimetric [XYZ/sRGB]");
    case SkyMeasurementType::Spectral:
        return QString::fromUtf8("Spectral");
    }

    return QString::fromUtf8("Unknown");
}

} // namespace

// ================================================================
// SkyPerspectiveWidget
// ================================================================

// 功能：创建透视天空控件并初始化天气动画定时器。
SkyPerspectiveWidget::SkyPerspectiveWidget(QWidget* parent) : QWidget(parent)
{
    setMinimumSize(520, 340);
    setMouseTracking(true);

    m_weatherTimer = new QTimer(this);
    m_weatherTimer->setInterval(33);
    connect(m_weatherTimer, &QTimer::timeout, this, &SkyPerspectiveWidget::advanceWeatherAnimation);
}

// 功能：设置并规范化全部天空、相机、传感器和显示参数。
void SkyPerspectiveWidget::setParameters(const SkyPerspectiveParameters& parameters)
{
    m_parameters = parameters;
    m_parameters.cieSkyType = std::max(0, std::min(14, m_parameters.cieSkyType));

    if (m_parameters.localCamera)
    {
        m_parameters.cameraAzimuthDeg = clamp(m_parameters.cameraAzimuthDeg, -180.0, 180.0);
        m_parameters.cameraPitchDeg = clamp(m_parameters.cameraPitchDeg, -180.0, 180.0);
    }
    else
    {
        while (m_parameters.cameraAzimuthDeg < 0.0)
        {
            m_parameters.cameraAzimuthDeg += 360.0;
        }
        while (m_parameters.cameraAzimuthDeg >= 360.0)
        {
            m_parameters.cameraAzimuthDeg -= 360.0;
        }
        m_parameters.cameraPitchDeg = clamp(m_parameters.cameraPitchDeg, -89.0, 89.0);
    }

    m_parameters.verticalFovDeg = clamp(m_parameters.verticalFovDeg, 10.0, 170.0);
    m_parameters.cameraRollDeg = clamp(m_parameters.cameraRollDeg, -180.0, 180.0);
    m_parameters.horizontalFovDeg = clamp(m_parameters.horizontalFovDeg, 10.0, 170.0);
    m_parameters.targetValue = std::max(0.0, m_parameters.targetValue);
    m_parameters.directNormalValue = std::max(0.0, m_parameters.directNormalValue);
    m_parameters.displayReferenceValue = std::max(1.0e-9, m_parameters.displayReferenceValue);
    m_parameters.exposure = std::max(0.001, m_parameters.exposure);
    m_parameters.gamma = std::max(0.1, m_parameters.gamma);
    m_parameters.sunAngularRadiusDeg = clamp(m_parameters.sunAngularRadiusDeg, 0.05, 5.0);
    if (!std::isfinite(m_parameters.skySphereRadius) || m_parameters.skySphereRadius <= 1.0e-6)
    {
        m_parameters.skySphereRadius = 1.0;
    }
    m_parameters.directIntegrationAngleDeg = clamp(m_parameters.directIntegrationAngleDeg, 0.01, 90.0);
    m_parameters.sensorFocalMm = std::max(0.001, m_parameters.sensorFocalMm);
    m_parameters.spectralTemperatureK = clamp(m_parameters.spectralTemperatureK, 1000.0, 20000.0);
    m_parameters.spectralStartNm = clamp(m_parameters.spectralStartNm, 350.0, 2000.0);
    m_parameters.spectralEndNm = clamp(m_parameters.spectralEndNm, 350.0, 2000.0);

    if (m_parameters.spectralEndNm < m_parameters.spectralStartNm)
    {
        std::swap(m_parameters.spectralEndNm, m_parameters.spectralStartNm);
    }
    m_parameters.spectralSampling = std::max(3, std::min(401, m_parameters.spectralSampling));
    m_parameters.displayWavelengthNm = clamp(m_parameters.displayWavelengthNm, m_parameters.spectralStartNm, m_parameters.spectralEndNm);

    if (std::abs(m_parameters.sensorXEndMm - m_parameters.sensorXStartMm) < 1.0e-9)
    {
        m_parameters.sensorXEndMm = m_parameters.sensorXStartMm + 0.001;
    }
    if (std::abs(m_parameters.sensorYEndMm - m_parameters.sensorYStartMm) < 1.0e-9)
    {
        m_parameters.sensorYEndMm = m_parameters.sensorYStartMm + 0.001;
    }

    if (m_parameters.sunDirection.lengthSquared() < 1.0e-12f)
    {
        m_parameters.sunDirection = QVector3D(0.0f, 0.0f, 1.0f);
    }
    m_parameters.sunDirection.normalize();

    // ENU 基采用 QVector3D 做 Gram-Schmidt 正交化。E/N/U 都可以由界面输入，但最终保证 E×N=U，避免非正交基造成天空方向和相机方向畸变。
    QVector3D east = m_parameters.skyEastDirection;
    QVector3D north = m_parameters.skyNorthDirection;
    const QVector3D requestedUp = m_parameters.skyZenithDirection;
    if (east.lengthSquared() < 1.0e-12f)
    {
        east = QVector3D(1.0f, 0.0f, 0.0f);
    }
    east.normalize();
    north -= QVector3D::dotProduct(north, east) * east;
    if (north.lengthSquared() < 1.0e-12f)
    {
        QVector3D fallbackUp = requestedUp.lengthSquared() > 1.0e-12f ? requestedUp.normalized() : QVector3D(0.0f, 0.0f, 1.0f);
        north = QVector3D::crossProduct(fallbackUp, east);
    }
    if (north.lengthSquared() < 1.0e-12f)
    {
        north = QVector3D(0.0f, 1.0f, 0.0f);
    }
    north.normalize();
    QVector3D up = QVector3D::crossProduct(east, north).normalized();
    if (requestedUp.lengthSquared() > 1.0e-12f && QVector3D::dotProduct(up, requestedUp) < 0.0f)
    {
        north = -north;
        up = -up;
    }
    m_parameters.skyEastDirection = east;
    m_parameters.skyNorthDirection = north;
    m_parameters.skyZenithDirection = up;

    m_parameters.weather.intensity = clamp(m_parameters.weather.intensity, 0.0, 1.0);
    m_parameters.weather.fogDensity = clamp(m_parameters.weather.fogDensity, 0.0, 1.0);
    if (m_parameters.animateWeather && weatherNeedsAnimation())
    {
        if (!m_weatherTimer->isActive())
        {
            m_weatherTimer->start();
        }
    }
    else
    {
        m_weatherTimer->stop();
    }

    rebuildPreview();
    update();
}

// 功能：返回当前已经规范化后的渲染参数。
const SkyPerspectiveParameters& SkyPerspectiveWidget::parameters() const
{
    return m_parameters;
}

// 功能：把当前渲染使用的“Viewer Camera -> World ENU”和“World -> CIE Sky ENU”几何状态导出给独立三维查看器。
SkySceneState SkyPerspectiveWidget::sceneState() const
{
    SkySceneState state;
    state.sphereRadius = m_parameters.skySphereRadius;
    state.finiteSkySphere = m_parameters.useFiniteSkySphere;
    state.skyEastDirection = m_parameters.skyEastDirection;
    state.skyNorthDirection = m_parameters.skyNorthDirection;
    state.skyUpDirection = m_parameters.skyZenithDirection;
    state.sunDirectionWorld = m_parameters.sunDirection.normalized();
    state.cameraOriginWorld = cameraOriginWorld();

    const double tanHalfHorizontal = std::tan(0.5 * m_parameters.horizontalFovDeg * kDegToRad);
    const double tanHalfVertical = std::tan(0.5 * m_parameters.verticalFovDeg * kDegToRad);
    const double cornerX[4] = {-tanHalfHorizontal, tanHalfHorizontal, tanHalfHorizontal, -tanHalfHorizontal};
    const double cornerY[4] = {tanHalfVertical, tanHalfVertical, -tanHalfVertical, -tanHalfVertical};

    if (m_parameters.localCamera)
    {
        QVector3D cameraX = localCameraVectorToWorldENU(QVector3D(1.0f, 0.0f, 0.0f)).normalized();
        QVector3D cameraY = localCameraVectorToWorldENU(QVector3D(0.0f, 1.0f, 0.0f)).normalized();
        QVector3D cameraForward = localCameraVectorToWorldENU(QVector3D(0.0f, 0.0f, 1.0f)).normalized();
        if (m_parameters.cameraRelativeToSkyBasis)
        {
            cameraX = skyToWorld(cameraX).normalized();
            cameraY = skyToWorld(cameraY).normalized();
            cameraForward = skyToWorld(cameraForward).normalized();
        }
        state.cameraXAxisWorld = cameraX;
        state.cameraYAxisWorld = cameraY;
        state.cameraForwardWorld = cameraForward;
        for (int index = 0; index < 4; ++index)
        {
            QVector3D ray = localCameraVectorToWorldENU(QVector3D(static_cast<float>(cornerX[index]), static_cast<float>(-cornerY[index]), 1.0f)).normalized();
            state.frustumDirectionsWorld[index] = m_parameters.cameraRelativeToSkyBasis ? skyToWorld(ray).normalized() : ray;
        }
        return state;
    }

    const double yaw = m_parameters.cameraAzimuthDeg * kDegToRad;
    const double pitch = m_parameters.cameraPitchDeg * kDegToRad;
    QVector3D forward(static_cast<float>(std::cos(pitch) * std::sin(yaw)), static_cast<float>(std::cos(pitch) * std::cos(yaw)), static_cast<float>(std::sin(pitch)));
    QVector3D right = QVector3D::crossProduct(forward, QVector3D(0.0f, 0.0f, 1.0f));
    right = right.lengthSquared() < 1.0e-10f ? QVector3D(1.0f, 0.0f, 0.0f) : right.normalized();
    QVector3D up = QVector3D::crossProduct(right, forward).normalized();
    const double roll = m_parameters.cameraRollDeg * kDegToRad;
    const QVector3D rolledRight = static_cast<float>(std::cos(roll)) * right + static_cast<float>(std::sin(roll)) * up;
    const QVector3D rolledUp = static_cast<float>(-std::sin(roll)) * right + static_cast<float>(std::cos(roll)) * up;
    if (m_parameters.cameraRelativeToSkyBasis)
    {
        forward = skyToWorld(forward).normalized();
        right = skyToWorld(rolledRight).normalized();
        up = skyToWorld(rolledUp).normalized();
    }
    else
    {
        right = rolledRight;
        up = rolledUp;
    }
    state.cameraXAxisWorld = right;
    state.cameraYAxisWorld = up;
    state.cameraForwardWorld = forward;
    for (int index = 0; index < 4; ++index)
    {
        QVector3D ray = (forward + static_cast<float>(cornerX[index]) * right + static_cast<float>(cornerY[index]) * up).normalized();
        state.frustumDirectionsWorld[index] = ray;
    }
    return state;
}

// 功能：把数值限制在指定闭区间。
double SkyPerspectiveWidget::clamp(double value, double low, double high)
{
    return std::max(low, std::min(value, high));
}

// 功能：生成稳定的 0~1 伪随机数，用于天气粒子位置。
double SkyPerspectiveWidget::hash01(int index, int salt)
{
    const double value = std::sin(index * 12.9898 + salt * 78.233) * 43758.5453123;
    return value - std::floor(value);
}

// 功能：在两个三维颜色向量之间做线性插值。
QVector3D SkyPerspectiveWidget::mix(const QVector3D& a, const QVector3D& b, double t)
{
    const float ratio = static_cast<float>(clamp(t, 0.0, 1.0));
    return a * (1.0f - ratio) + b * ratio;
}

// 功能：把世界方向投影到天空局部 ENU 基。
QVector3D SkyPerspectiveWidget::worldToSky(const QVector3D& worldDirection) const
{
    return QVector3D(QVector3D::dotProduct(worldDirection, m_parameters.skyEastDirection), QVector3D::dotProduct(worldDirection, m_parameters.skyNorthDirection), QVector3D::dotProduct(worldDirection, m_parameters.skyZenithDirection));
}

// 功能：把天空局部 ENU 方向转换为世界方向。
QVector3D SkyPerspectiveWidget::skyToWorld(const QVector3D& skyDirection) const
{
    return skyDirection.x() * m_parameters.skyEastDirection + skyDirection.y() * m_parameters.skyNorthDirection + skyDirection.z() * m_parameters.skyZenithDirection;
}

// 功能：只旋转天空局部 ENU 向量到世界坐标，不改变向量长度；有限天空球中的相机位置必须保留真实位移长度，不能像方向向量那样归一化。
QVector3D SkyPerspectiveWidget::skyVectorToWorld(const QVector3D& skyVector) const
{
    return skyToWorld(skyVector);
}

// 功能：计算给定世界方向上的 CIE Sky 相对亮度。
//
// CIE Standard General Sky 的核心是“高度梯度 × 太阳附近散射”两部分：
//   z   = acos(P.z)       ：当前天空方向 P 距离天顶的天顶角；
//   Zs  = acos(S.z)       ：太阳方向 S 的天顶角；
//   chi = acos(P · S)     ：P 与太阳之间的真实三维夹角。
// SunSky 内部按所选 CIE Type 的 a,b,c,d,e 系数计算
//   phi(z) = 1 + a * exp(b / cos(z))
//   f(chi) = 1 + c * [exp(d*chi) - exp(d*pi/2)] + e*cos(chi)^2
// 并用天顶值归一化：Lrel(P)=phi(z)f(chi)/[phi(0)f(Zs)]。
// 因而这里只得到“相对亮度形状”，真正的绝对亮度/辐亮度由 absoluteScale() 完成。
double SkyPerspectiveWidget::relativeSkyValue(const QVector3D& direction) const
{
    if (direction.lengthSquared() < 1.0e-12f)
    {
        return 0.0;
    }

    const QVector3D skyDirection = worldToSky(direction.normalized());
    if (skyDirection.z() <= 0.0f)
    {
        return 0.0;
    }

    // StandardSkyViewer 传入 EnvironmentLight 时直接复用 environment_light.h 的 CIESkyModel。Direction.theta 是天顶角，phi 使用 ENU 方位角 atan2(E,N)，因此无需在 Viewer 内再次展开 a,b,c,d,e 公式。
    if (m_parameters.environmentLight)
    {
        const double theta = std::acos(clamp(static_cast<double>(skyDirection.z()), -1.0, 1.0));
        double phi = std::atan2(static_cast<double>(skyDirection.x()), static_cast<double>(skyDirection.y()));
        if (phi < 0.0)
        {
            phi += 2.0 * kPi;
        }
        const double value = m_parameters.environmentLight->getRadiance(Direction(theta, phi));
        return std::isfinite(value) ? std::max(0.0, value) : 0.0;
    }

    // CIEWidget 仍保留原 SunSky 路径，以支持 EPW DHI/照度标定和自定义 A-E 系数。
    const QVector3D skySun = worldToSky(m_parameters.sunDirection.normalized());
    const SSLib::Vec3f sky = toSunSky(skyDirection);
    const SSLib::Vec3f sun = toSunSky(skySun);
    const double value = m_parameters.customCoefficients ? SSLib::CIECustomSky(m_parameters.coefficients, sky, sun, 1.0f) : SSLib::CIEStandardSky(m_parameters.cieSkyType, sky, sun, 1.0f);
    return std::isfinite(value) ? std::max(0.0, value) : 0.0;
}

// 功能：根据绝对标定方式计算 CIE 相对分布到真实量纲的比例系数。
//
// 绝对标定只缩放漫射天空，不改变 CIE Type 决定的方向分布：
// - ZenithLuminance：让天顶方向恰好等于目标 Lz；
// - DiffuseHorizontalIlluminance/Irradiance：让整个天空半球在水平面上的积分
//   等于目标 DHI/散射照度。太阳直射量由太阳盘独立处理。
double SkyPerspectiveWidget::absoluteScale() const
{
    if (m_parameters.targetValue <= 0.0)
        return 0.0;

    if (m_parameters.scaleMode == SkyAbsoluteScaleMode::ZenithLuminance) {
        // Lrel 已按天顶归一化，但仍实际求一次天顶值，可兼容自定义系数和数值误差。
        const double zenithRelative = relativeSkyValue(m_parameters.skyZenithDirection);
        return zenithRelative > 1.0e-12 ? m_parameters.targetValue / zenithRelative : 0.0;
    }

    // 水平漫射量满足 E_h = ∫hemisphere L(ω) cos(theta) dΩ。
    // 这里用高度角 h 积分：theta=pi/2-h，cos(theta)=sin(h)，
    // 同时 dΩ=cos(h) dh dAz，因此离散权重正好是 sin(h)*cos(h)*dh*dAz。
    const int altitudeSteps = 72;
    const int azimuthSteps = 288;
    const double dAltitude = (0.5 * kPi) / altitudeSteps;
    const double dAzimuth = (2.0 * kPi) / azimuthSteps;

    double integral = 0.0;

    for (int altitudeIndex = 0; altitudeIndex < altitudeSteps; ++altitudeIndex) {
        const double altitude = (altitudeIndex + 0.5) * dAltitude;
        const double sinAltitude = std::sin(altitude);
        const double cosAltitude = std::cos(altitude);

        for (int azimuthIndex = 0; azimuthIndex < azimuthSteps; ++azimuthIndex) {
            const double azimuth = (azimuthIndex + 0.5) * dAzimuth;

            const QVector3D localDirection(static_cast<float>(cosAltitude * std::sin(azimuth)), static_cast<float>(cosAltitude * std::cos(azimuth)), static_cast<float>(sinAltitude));
            const QVector3D worldDirection = skyToWorld(localDirection);

            integral += relativeSkyValue(worldDirection) * sinAltitude * cosAltitude * dAltitude * dAzimuth;
        }
    }

    return integral > 1.0e-12 ? m_parameters.targetValue / integral : 0.0;
}

// 功能：把物理标量压缩到 0~1 的显示亮度。
double SkyPerspectiveWidget::toneMappedValue(double value, double referenceValue) const
{
    if (value <= 0.0 || referenceValue <= 0.0)
        return 0.0;

    const double mapped = 1.0 - std::exp(-m_parameters.exposure * value / referenceValue);

    return clamp(std::pow(clamp(mapped, 0.0, 1.0), 1.0 / m_parameters.gamma), 0.0, 1.0);
}

// 功能：根据 CIE 类型估计自然预览的晴朗程度。
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

// 功能：根据降水和雾估计直射太阳的显示衰减。
double SkyPerspectiveWidget::atmosphericAttenuation() const
{
    const double precipitation = m_parameters.weather.precipitation == PrecipitationKind::None ? 0.0 : m_parameters.weather.intensity;
    const double fog = m_parameters.weather.fogDensity;
    return std::exp(-1.5 * precipitation - 2.2 * fog);
}

// 功能：把雾、云量和降水造成的大气泛白应用到预览颜色。
QColor SkyPerspectiveWidget::applyWeatherAtmosphere(const QColor& source) const
{
    const double precipitation = m_parameters.weather.precipitation == PrecipitationKind::None ? 0.0 : m_parameters.weather.intensity;
    const double fog = m_parameters.weather.fogDensity;
    const double cloud = clamp(m_parameters.weather.opaqueSkyCoverTenths / 10.0, 0.0, 1.0);

    const double blend = clamp(0.16 * precipitation + 0.72 * fog + 0.10 * cloud, 0.0, 0.88);

    QColor atmosphere(183, 190, 197);
    if (m_parameters.weather.precipitation == PrecipitationKind::Snow)
        atmosphere = QColor(208, 214, 220);

    return QColor::fromRgbF(source.redF() * (1.0 - blend) + atmosphere.redF() * blend, source.greenF() * (1.0 - blend) + atmosphere.greenF() * blend, source.blueF() * (1.0 - blend) + atmosphere.blueF() * blend, source.alphaF());
}

// 功能：根据积雪或降雨状态返回地面显示颜色。
QColor SkyPerspectiveWidget::groundColor() const
{
    if (!m_parameters.showWeatherGround)
        return m_parameters.baseGroundColor;

    const bool fallingSnow = m_parameters.weather.precipitation == PrecipitationKind::Snow || m_parameters.weather.precipitation == PrecipitationKind::Mixed;

    if (m_parameters.weather.snowDepthCm > 0.0 || fallingSnow) {
        const double coverage = clamp(std::max(m_parameters.weather.snowDepthCm / 5.0, fallingSnow ? 0.45 * m_parameters.weather.intensity : 0.0), 0.0, 1.0);
        const double albedo = clamp(m_parameters.weather.groundAlbedo, 0.2, 0.95);
        const int value = static_cast<int>(80.0 + 165.0 * coverage * albedo);
        return QColor(value, value, std::min(255, value + 5));
    }

    if (m_parameters.weather.precipitation == PrecipitationKind::Rain || m_parameters.weather.precipitation == PrecipitationKind::Mixed || m_parameters.weather.precipitation == PrecipitationKind::FreezingRain) {
        return QColor(22, 27, 31);
    }

    return m_parameters.baseGroundColor;
}

// 功能：判断当前天气是否需要启动粒子动画。
bool SkyPerspectiveWidget::weatherNeedsAnimation() const
{
    if (!m_parameters.showWeatherParticles)
        return false;

    return m_parameters.weather.intensity > 0.0 && m_parameters.weather.precipitation != PrecipitationKind::None;
}

// 功能：为输出像素构造世界坐标中的单位相机射线。
//
// 导航相机采用与 CIE/EPW 一致的 ENU 语义：Az=0° 朝 North，90° 朝 East，
// Pitch>0 抬头。先由 Az/Pitch 得 forward，再由叉乘得到 right/up，随后 Roll
// 只绕 forward 旋转画面基；最后用 HFOV/VFOV 把像素坐标转换为方向射线。
QVector3D SkyPerspectiveWidget::cameraRay(int x, int y, int width, int height) const
{
    const int safeWidth = std::max(1, width);
    const int safeHeight = std::max(1, height);

    if (m_parameters.useSensorFrameProjection)
    {
        QVector3D sensorX = m_parameters.sensorXAxisWorld;
        if (sensorX.lengthSquared() < 1.0e-12f)
        {
            sensorX = QVector3D(1.0f, 0.0f, 0.0f);
        }
        sensorX.normalize();
        QVector3D sensorY = m_parameters.sensorYAxisWorld;
        sensorY -= QVector3D::dotProduct(sensorY, sensorX) * sensorX;
        if (sensorY.lengthSquared() < 1.0e-12f)
        {
            sensorY = std::abs(sensorX.y()) < 0.9f ? QVector3D(0.0f, 1.0f, 0.0f) : QVector3D(0.0f, 0.0f, 1.0f);
            sensorY -= QVector3D::dotProduct(sensorY, sensorX) * sensorX;
        }
        sensorY.normalize();
        QVector3D sensorNormal = QVector3D::crossProduct(sensorX, sensorY);
        sensorNormal = sensorNormal.lengthSquared() < 1.0e-12f ? QVector3D(0.0f, 0.0f, 1.0f) : sensorNormal.normalized();
        const double u = (x + 0.5) / safeWidth;
        const double v = (y + 0.5) / safeHeight;
        const double sensorCoordX = m_parameters.sensorXStartMm + u * (m_parameters.sensorXEndMm - m_parameters.sensorXStartMm);
        const double sensorCoordY = m_parameters.sensorYEndMm - v * (m_parameters.sensorYEndMm - m_parameters.sensorYStartMm);
        return (static_cast<float>(m_parameters.sensorFocalMm) * sensorNormal + static_cast<float>(sensorCoordX) * sensorX + static_cast<float>(sensorCoordY) * sensorY).normalized();
    }

    const double tanHalfHorizontal = std::tan(0.5 * m_parameters.horizontalFovDeg * kDegToRad);
    const double tanHalfVertical = std::tan(0.5 * m_parameters.verticalFovDeg * kDegToRad);
    const double screenX = (2.0 * (x + 0.5) / safeWidth - 1.0) * tanHalfHorizontal;
    const double screenY = (1.0 - 2.0 * (y + 0.5) / safeHeight) * tanHalfVertical;

    if (m_parameters.localCamera)
    {
        // Local Camera 与主界面保持同一语义：传统相机 +Zc 为 forward，Euler 顺序为 Rz(Yaw)*Ry(Pitch)*Rx(Roll)，零姿态时 Xc->North、Yc->Up、Zc->East。
        const QVector3D worldEnuRay = localCameraVectorToWorldENU(QVector3D(static_cast<float>(screenX), static_cast<float>(-screenY), 1.0f)).normalized();
        return m_parameters.cameraRelativeToSkyBasis ? skyToWorld(worldEnuRay).normalized() : worldEnuRay;
    }

    // Non-local Camera 是标准 ENU 导航相机：Az=0° North、90° East，Pitch>0 向上，Roll 仅绕 forward 旋转画面。
    const double yaw = m_parameters.cameraAzimuthDeg * kDegToRad;
    const double pitch = m_parameters.cameraPitchDeg * kDegToRad;
    const QVector3D forward(static_cast<float>(std::cos(pitch) * std::sin(yaw)), static_cast<float>(std::cos(pitch) * std::cos(yaw)), static_cast<float>(std::sin(pitch)));
    QVector3D right = QVector3D::crossProduct(forward, QVector3D(0.0f, 0.0f, 1.0f));
    right = right.lengthSquared() < 1.0e-10f ? QVector3D(1.0f, 0.0f, 0.0f) : right.normalized();
    const QVector3D up = QVector3D::crossProduct(right, forward).normalized();
    const double roll = m_parameters.cameraRollDeg * kDegToRad;
    const double cosRoll = std::cos(roll);
    const double sinRoll = std::sin(roll);
    const QVector3D rolledRight = static_cast<float>(cosRoll) * right + static_cast<float>(sinRoll) * up;
    const QVector3D rolledUp = static_cast<float>(-sinRoll) * right + static_cast<float>(cosRoll) * up;
    const QVector3D localRay = (forward + static_cast<float>(screenX) * rolledRight + static_cast<float>(screenY) * rolledUp).normalized();
    return m_parameters.cameraRelativeToSkyBasis ? skyToWorld(localRay).normalized() : localRay;
}

// 功能：把传统 Local Camera 向量按与主界面一致的 Euler 语义转换到世界 ENU。
QVector3D SkyPerspectiveWidget::localCameraVectorToWorldENU(const QVector3D& cameraVector) const
{
    const double yaw = m_parameters.cameraAzimuthDeg * kDegToRad;
    const double pitch = m_parameters.cameraPitchDeg * kDegToRad;
    const double roll = m_parameters.cameraRollDeg * kDegToRad;
    const double cr = std::cos(roll);
    const double sr = std::sin(roll);
    const double cp = std::cos(pitch);
    const double sp = std::sin(pitch);
    const double cy = std::cos(yaw);
    const double sy = std::sin(yaw);
    const double x1 = cameraVector.x();
    const double y1 = cr * cameraVector.y() - sr * cameraVector.z();
    const double z1 = sr * cameraVector.y() + cr * cameraVector.z();
    const double x2 = cp * x1 + sp * z1;
    const double y2 = y1;
    const double z2 = -sp * x1 + cp * z1;
    const double x3 = cy * x2 - sy * y2;
    const double y3 = sy * x2 + cy * y2;
    const double z3 = z2;
    return QVector3D(static_cast<float>(z3), static_cast<float>(x3), static_cast<float>(y3));
}

// 功能：把 Xc/Yc/Zc 转换成有限天空球中的世界 ENU 相机原点。
QVector3D SkyPerspectiveWidget::cameraOriginWorld() const
{
    // Local Camera=true 时，位移沿相机自身 Xc/Yc/Zc 轴移动，因此 RPY 改变后位移在 ENU 中的方向也会改变；false 时 Xc/Yc/Zc 直接解释为 ENU East/North/Up。
    const QVector3D originWorldENU = m_parameters.localCamera ? localCameraVectorToWorldENU(m_parameters.cameraPositionLocal) : m_parameters.cameraPositionLocal;
    return m_parameters.cameraRelativeToSkyBasis ? skyVectorToWorld(originWorldENU) : originWorldENU;
}

// 功能：计算射线与以世界原点为球心的有限天空球交点。
bool SkyPerspectiveWidget::raySphereIntersection(const QVector3D& origin, const QVector3D& rayDirection, double radius, QVector3D& sphereDirection)
{
    if (!std::isfinite(radius) || radius <= 1.0e-6 || rayDirection.lengthSquared() < 1.0e-12f)
    {
        return false;
    }
    const QVector3D ray = rayDirection.normalized();
    const double a = QVector3D::dotProduct(ray, ray);
    const double b = 2.0 * QVector3D::dotProduct(origin, ray);
    const double c = QVector3D::dotProduct(origin, origin) - radius * radius;
    const double discriminant = b * b - 4.0 * a * c;
    if (discriminant < 0.0)
    {
        return false;
    }
    const double root = std::sqrt(discriminant);
    const double t1 = (-b - root) / (2.0 * a);
    const double t2 = (-b + root) / (2.0 * a);
    const double t = t1 > 1.0e-6 ? t1 : (t2 > 1.0e-6 ? t2 : -1.0);
    if (t <= 1.0e-6)
    {
        return false;
    }
    const QVector3D hitPoint = origin + static_cast<float>(t) * ray;
    if (hitPoint.lengthSquared() < 1.0e-12f)
    {
        return false;
    }
    sphereDirection = hitPoint.normalized();
    return true;
}

// 功能：返回当前像素真正用于查询 CIE 天空的方向。
bool SkyPerspectiveWidget::sampleDirectionForPixel(int x, int y, int width, int height, const QVector3D& sphereOriginWorld, QVector3D& direction) const
{
    const QVector3D ray = cameraRay(x, y, width, height);
    if (!m_parameters.useFiniteSkySphere)
    {
        direction = ray;
        return true;
    }
    // CIE 模型仍然只接收方向。有限天空球只属于 Viewer 几何层：origin + t*ray 先与半径 R 的球求交，再用“球心 -> 交点”方向查询 CIE。这样相机平移会改变采样方向，但不会修改 CIE 15 类天空公式本身。
    return raySphereIntersection(sphereOriginWorld, ray, m_parameters.skySphereRadius, direction);
}

// 功能：把 0~1 标量映射为伪彩色。
QColor SkyPerspectiveWidget::falseColor(double value)
{
    const double t = clamp(value, 0.0, 1.0);
    const double red = clamp(1.5 - std::abs(4.0 * t - 3.0), 0.0, 1.0);
    const double green = clamp(1.5 - std::abs(4.0 * t - 2.0), 0.0, 1.0);
    const double blue = clamp(1.5 - std::abs(4.0 * t - 1.0), 0.0, 1.0);
    return QColor::fromRgbF(red, green, blue);
}

// 功能：用 CIE 标量亮度生成不参与数值计算的自然天空预览颜色。
QColor SkyPerspectiveWidget::naturalPreviewColor(const QVector3D& direction, double normalizedBrightness, double sunCosine, double directStrength) const
{
    // Natural preview 与 CIE Perspective Sky 使用同一套“地平线到天顶渐变 + CIE 明暗 + 太阳暖色光晕”的显示模型。environment_light.h 仍负责 CIE 数值分布，但这里不直接采用 CIESkyModel::getRGB() 的色彩，以保证两个 CIE 透视界面的自然预览观感一致。
    const QVector3D localDirection = worldToSky(direction.normalized());
    const QVector3D localSun = worldToSky(m_parameters.sunDirection.normalized());
    const double clarity = clearSkyFactor();
    const double altitudeFactor = std::pow(clamp(static_cast<double>(localDirection.z()), 0.0, 1.0), 0.35);

    const QVector3D overcastHorizon(0.70f, 0.72f, 0.74f);
    const QVector3D overcastZenith(0.55f, 0.59f, 0.63f);
    const QVector3D clearHorizon(0.68f, 0.82f, 0.98f);
    const QVector3D clearZenith(0.10f, 0.30f, 0.76f);

    const QVector3D horizonColor = mix(overcastHorizon, clearHorizon, clarity);
    const QVector3D zenithColor = mix(overcastZenith, clearZenith, clarity);

    QVector3D rgb = mix(horizonColor, zenithColor, altitudeFactor);

    if (m_parameters.showSunGlow && localSun.z() > 0.0f) {
        const double sunAngle = std::acos(clamp(sunCosine, -1.0, 1.0));
        const double glowWidth = (5.0 + 16.0 * (1.0 - clarity)) * kDegToRad;
        const double glow = std::exp(-sunAngle / std::max(1.0e-6, glowWidth)) * directStrength * (0.25 + 0.75 * clarity);

        const QVector3D warmWhite(1.0f, 0.92f, 0.72f);
        rgb = mix(rgb, warmWhite, clamp(glow * 0.65, 0.0, 0.75));
    }

    const double brightness = clamp(normalizedBrightness * 1.20, 0.0, 1.0);
    rgb *= static_cast<float>(brightness);

    return QColor::fromRgbF(clamp(rgb.x(), 0.0, 1.0), clamp(rgb.y(), 0.0, 1.0), clamp(rgb.z(), 0.0, 1.0));
}

// 功能：渲染不含粒子覆盖层的天空/地面基础图像。
QImage SkyPerspectiveWidget::renderBaseImage(const QSize& imageSize) const
{
    const int width = std::max(1, imageSize.width());
    const int height = std::max(1, imageSize.height());
    const QVector3D sphereOriginWorld = m_parameters.useFiniteSkySphere ? cameraOriginWorld() : QVector3D();

    QImage image(width, height, QImage::Format_ARGB32);
    image.fill(QColor(28, 30, 34));

    // 第一步先把 CIE 相对天空按 Lz/DHI/散射照度变成绝对量；
    // 后面的 spectral/tone mapping 只负责单位转换和显示，不反向改变 CIE 分布。
    const double diffuseSourceScale = absoluteScale();
    const SpectralConversion spectral = buildSpectralConversion(m_parameters);

    const QVector3D sun = m_parameters.sunDirection.normalized();
    const QVector3D sunLocal = worldToSky(sun);
    const double sunRadiusRadians = m_parameters.sunAngularRadiusDeg * kDegToRad;
    const double cosSunRadius = std::cos(sunRadiusRadians);
    // 太阳盘不属于 CIE 漫射天空分布。把太阳看成角半径 alpha 的球面圆盘：
    // Ωsun = 2*pi*(1-cos(alpha))。DNI/直射法向照度除以 Ωsun 后，才得到
    // 太阳盘内近似均匀的方向辐亮度/亮度；这样太阳和漫射天空可独立闭环标定。
    const double sunSolidAngle = 2.0 * kPi * (1.0 - std::cos(sunRadiusRadians));

    const double effectiveDirectNormal = m_parameters.directNormalValue * atmosphericAttenuation();
    const double directDiskSourceValue = sunSolidAngle > 1.0e-12 ? effectiveDirectNormal / sunSolidAngle : 0.0;

    const bool includeDiffuse = m_parameters.measurementLayer != SkyMeasurementLayer::DirectSunOnly;
    const bool includeDirect = m_parameters.measurementLayer != SkyMeasurementLayer::DiffuseSkyOnly && effectiveDirectNormal > 0.0 && sunLocal.z() > 0.0f;
    const bool previewSunDisk = m_parameters.showSunDisk && effectiveDirectNormal > 0.0 && sunLocal.z() > 0.0f;

    // Measurement Layer 只控制物理数值层；Show sun disk / Show sun glow 只控制自然预览显示，不再删除 Direct sun only 的物理太阳值。Natural Preview 另外保存完整漫射天空作为观察辅助背景，因此 Direct sun only 不会再黑屏。
    QVector<double> samples(width * height, 0.0);
    QVector<double> previewDiffuseSamples(width * height, 0.0);
    double resultPeak = 0.0;
    double diffusePeak = 0.0;
    double previewDiffusePeak = 0.0;

    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            QVector3D direction;
            if (!sampleDirectionForPixel(x, y, width, height, sphereOriginWorld, direction))
            {
                continue;
            }
            if (worldToSky(direction).z() <= 0.0f)
            {
                continue;
            }

            const double fullDiffuseSourceValue = relativeSkyValue(direction) * diffuseSourceScale;
            const double diffuseSourceValue = includeDiffuse ? fullDiffuseSourceValue : 0.0;
            const double previewDiffuseValue = convertSourceValue(fullDiffuseSourceValue, m_parameters, spectral);
            previewDiffuseSamples[y * width + x] = previewDiffuseValue;
            previewDiffusePeak = std::max(previewDiffusePeak, previewDiffuseValue);

            double directSourceValue = 0.0;
            if (includeDirect)
            {
                const double sunCosine = QVector3D::dotProduct(direction, sun);
                if (sunCosine >= cosSunRadius)
                {
                    directSourceValue = directDiskSourceValue;
                }
            }

            const double value = convertSourceValue(diffuseSourceValue + directSourceValue, m_parameters, spectral);
            samples[y * width + x] = value;
            resultPeak = std::max(resultPeak, value);
            if (includeDiffuse)
            {
                diffusePeak = std::max(diffusePeak, convertSourceValue(diffuseSourceValue, m_parameters, spectral));
            }
        }
    }

    // Grayscale/False Color 继续严格反映 Measurement Layer 数值；Natural Preview 使用独立的完整漫射天空参考值。
    const double autoPeak = diffusePeak > 1.0e-12 ? diffusePeak : resultPeak;
    const double referenceValue = m_parameters.toneMapMode == SkyToneMapMode::AutoPeak ? std::max(1.0e-12, autoPeak) : std::max(1.0e-12, m_parameters.displayReferenceValue);
    const double previewReferenceValue = m_parameters.toneMapMode == SkyToneMapMode::AutoPeak ? std::max(1.0e-12, previewDiffusePeak) : std::max(1.0e-12, m_parameters.displayReferenceValue);

    // 80000 lx 只用于 Natural Preview 的太阳光晕显示归一化，不改变 Direct sun only 的物理数值。
    const double directStrength = clamp(effectiveDirectNormal / 80000.0, 0.0, 1.0);

    for (int y = 0; y < height; ++y)
    {
        QRgb* scanline = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < width; ++x)
        {
            QVector3D direction;
            if (!sampleDirectionForPixel(x, y, width, height, sphereOriginWorld, direction))
            {
                scanline[x] = QColor(18, 20, 23).rgba();
                continue;
            }
            if (worldToSky(direction).z() <= 0.0f)
            {
                scanline[x] = groundColor().rgba();
                continue;
            }

            const double value = samples[y * width + x];
            const double normalized = toneMappedValue(value, referenceValue);
            const double previewNormalized = toneMappedValue(previewDiffuseSamples[y * width + x], previewReferenceValue);
            const double sunCosine = QVector3D::dotProduct(direction, sun);

            QColor color;
            switch (m_parameters.colorMode)
            {
            case SkyColorMode::FalseColor:
                color = falseColor(normalized);
                break;
            case SkyColorMode::NaturalPreview:
                // Natural Preview 是几何/视觉辅助层，不再由 Measurement Layer 把背景清零；Sensor 类型和 Layer 仍保留在 samples 中供数值显示逻辑使用。
                color = naturalPreviewColor(direction, previewNormalized, sunCosine, directStrength);
                if (previewSunDisk && sunCosine >= cosSunRadius)
                {
                    const double warm = clamp(0.90 + 0.10 * directStrength, 0.0, 1.0);
                    color = QColor::fromRgbF(warm, warm * 0.97, warm * 0.86);
                }
                break;
            case SkyColorMode::GrayscaleLuminance:
            default:
                color = QColor::fromRgbF(normalized, normalized, normalized);
                break;
            }

            color = applyWeatherAtmosphere(color);
            scanline[x] = color.rgba();
        }
    }

    return image;
}

// 功能：在已经生成的图像上绘制雨、雪或冰雹粒子。
void SkyPerspectiveWidget::drawWeatherOverlay(QPainter& painter, const QRectF& targetRect, double animationSeconds) const
{
    if (!m_parameters.showWeatherParticles || m_parameters.weather.intensity <= 0.0) {
        return;
    }

    const PrecipitationKind kind = m_parameters.weather.precipitation;
    if (kind == PrecipitationKind::None)
        return;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setClipRect(targetRect);

    const double intensity = m_parameters.weather.intensity;
    const double width = targetRect.width();
    const double height = targetRect.height();

    // EPW 风向是“风从哪里来”，屏幕粒子移动方向需要反转 180°。
    const double relativeWind = (m_parameters.weather.windDirectionDeg + 180.0 - m_parameters.cameraAzimuthDeg) * kDegToRad;
    const double windSpeed = m_parameters.weather.windSpeedMps;
    const double screenWind = std::sin(relativeWind) * clamp(windSpeed / 12.0, 0.0, 1.8);

    const bool drawRain = kind == PrecipitationKind::Rain || kind == PrecipitationKind::Mixed || kind == PrecipitationKind::FreezingRain;
    const bool drawSnow = kind == PrecipitationKind::Snow || kind == PrecipitationKind::Mixed;
    const bool drawHail = kind == PrecipitationKind::Hail;

    if (drawRain) {
        const int count = static_cast<int>(80 + 620 * intensity);
        const double speed = 0.55 + 1.25 * intensity;

        QPen pen(QColor(205, 224, 238, static_cast<int>(80 + 110 * intensity)));
        pen.setWidthF(0.7 + 1.0 * intensity);
        pen.setCapStyle(Qt::RoundCap);
        painter.setPen(pen);

        for (int index = 0; index < count; ++index)
        {
            const double seedX = hash01(index, 1);
            const double seedY = hash01(index, 2);
            const double speedScale = 0.75 + 0.55 * hash01(index, 3);
            const double y = wrapUnit(seedY + animationSeconds * speed * speedScale);
            const double x = wrapUnit(seedX + animationSeconds * screenWind * 0.035 + y * screenWind * 0.055);

            const double pixelX = targetRect.left() + x * width;
            const double pixelY = targetRect.top() + y * height;
            const double length = (8.0 + 22.0 * intensity) * speedScale;
            const double deltaX = screenWind * length * 0.75;

            painter.drawLine(QPointF(pixelX - deltaX, pixelY - length), QPointF(pixelX, pixelY));
        }
    }

    if (drawSnow) {
        const int count = static_cast<int>(45 + 360 * intensity);
        const double speed = 0.10 + 0.28 * intensity;
        painter.setPen(Qt::NoPen);

        for (int index = 0; index < count; ++index)
        {
            const double seedX = hash01(index, 11);
            const double seedY = hash01(index, 12);
            const double seedSize = hash01(index, 13);
            const double seedPhase = hash01(index, 14) * 2.0 * kPi;
            const double y = wrapUnit(seedY + animationSeconds * speed * (0.7 + seedSize));
            const double flutter = std::sin(animationSeconds * (0.7 + seedSize) + seedPhase) * (0.008 + 0.018 * seedSize);
            const double x = wrapUnit(seedX + animationSeconds * screenWind * 0.014 + flutter);

            const double pixelX = targetRect.left() + x * width;
            const double pixelY = targetRect.top() + y * height;
            const double radius = 1.1 + 3.4 * seedSize;
            const int alpha = static_cast<int>(120 + 110 * seedSize);

            painter.setBrush(QColor(248, 251, 255, alpha));
            painter.drawEllipse(QPointF(pixelX, pixelY), radius, radius);
        }
    }

    if (drawHail) {
        const int count = static_cast<int>(40 + 260 * intensity);
        painter.setPen(QPen(QColor(230, 239, 247, 210), 0.8));
        painter.setBrush(QColor(221, 234, 244, 190));

        for (int index = 0; index < count; ++index)
        {
            const double seedX = hash01(index, 21);
            const double seedY = hash01(index, 22);
            const double y = wrapUnit(seedY + animationSeconds * (0.8 + intensity));
            const double x = wrapUnit(seedX + animationSeconds * screenWind * 0.025);
            const double radius = 1.3 + 2.6 * hash01(index, 23);

            painter.drawEllipse(QPointF(targetRect.left() + x * width, targetRect.top() + y * height), radius, radius);
        }
    }

    painter.restore();
}

// 功能：按指定分辨率离屏渲染天空图像。
QImage SkyPerspectiveWidget::renderToImage(const QSize& imageSize) const
{
    QImage image = renderBaseImage(imageSize);
    QPainter painter(&image);
    drawWeatherOverlay(painter, QRectF(0.0, 0.0, image.width(), image.height()), m_animationSeconds);
    return image;
}

// 功能：按指定分辨率渲染并保存 PNG 文件。
bool SkyPerspectiveWidget::savePng(const QString& filePath, const QSize& imageSize) const
{
    return renderToImage(imageSize).save(filePath, "PNG");
}

// 功能：重建用于窗口显示的低成本预览图。
void SkyPerspectiveWidget::rebuildPreview()
{
    if (width() <= 0 || height() <= 0)
        return;

    const QSize previewSize = size().boundedTo(QSize(800, 520));
    m_preview = renderBaseImage(previewSize);
}

// 功能：绘制缓存天空、天气覆盖层、地平线与状态文字。
void SkyPerspectiveWidget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.fillRect(rect(), QColor(28, 30, 34));

    if (m_preview.isNull())
        rebuildPreview();

    if (!m_preview.isNull())
        painter.drawImage(rect(), m_preview);

    drawWeatherOverlay(painter, QRectF(rect()), m_animationSeconds);

    painter.setRenderHint(QPainter::Antialiasing, true);

    if (m_parameters.showHorizon) {
        QPainterPath horizonPath;
        bool started = false;
        const int samples = std::max(64, width());
        const QVector3D sphereOriginWorld = m_parameters.useFiniteSkySphere ? cameraOriginWorld() : QVector3D();

        for (int sample = 0; sample < samples; ++sample)
        {
            const int x = static_cast<int>(sample * (width() - 1.0) / std::max(1, samples - 1));

            int bestY = -1;
            double bestAbsoluteZ = std::numeric_limits<double>::max();

            for (int y = 0; y < height(); y += 2)
            {
                QVector3D direction;
                if (!sampleDirectionForPixel(x, y, width(), height(), sphereOriginWorld, direction))
                {
                    continue;
                }
                const double absoluteZ = std::abs(worldToSky(direction).z());
                if (absoluteZ < bestAbsoluteZ)
                {
                    bestAbsoluteZ = absoluteZ;
                    bestY = y;
                }
            }

            if (bestY >= 0 && bestAbsoluteZ < 0.05) {
                if (!started) {
                    horizonPath.moveTo(x, bestY);
                    started = true;
                }
                else {
                    horizonPath.lineTo(x, bestY);
                }
            }
        }

        painter.setPen(QPen(QColor(255, 255, 255, 150), 1.0));
        painter.drawPath(horizonPath);
    }

    painter.setPen(QColor(255, 255, 255, 220));
    if (m_parameters.useSensorFrameProjection) {
        painter.drawText(12, 22, QString("Frame / Focal sensor   %1   f=%2 mm") .arg(measurementTypeText(m_parameters.measurementType)) .arg(m_parameters.sensorFocalMm, 0, 'f', 2));
    }
    else {
        painter.drawText(12, 22, QString("View Az %1°  View Alt %2°  VFOV %3°") .arg(m_parameters.cameraAzimuthDeg, 0, 'f', 1) .arg(m_parameters.cameraPitchDeg, 0, 'f', 1) .arg(m_parameters.verticalFovDeg, 0, 'f', 1));
    }

    if (!m_parameters.weather.description.isEmpty()) {
        painter.drawText(12, 43, QString("Weather: %1") .arg(m_parameters.weather.description));
    }
}

// 功能：推进雨雪粒子动画时间并触发重绘。
void SkyPerspectiveWidget::advanceWeatherAnimation()
{
    m_animationSeconds += 0.033;
    if (m_animationSeconds > 10000.0)
        m_animationSeconds = 0.0;
    update();
}

// 功能：窗口尺寸变化时重建交互预览缓存。
void SkyPerspectiveWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    rebuildPreview();
}

// 功能：记录导航相机拖拽起点。
void SkyPerspectiveWidget::mousePressEvent(QMouseEvent* event)
{
    if (m_parameters.useSensorFrameProjection) {
        event->ignore();
        return;
    }

    m_lastMousePosition = event->pos();
    event->accept();
}

// 功能：左键拖拽时调整相机方位角与仰角。
void SkyPerspectiveWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (m_parameters.useSensorFrameProjection) {
        event->ignore();
        return;
    }

    if (!(event->buttons() & Qt::LeftButton))
        return;

    const QPoint delta = event->pos() - m_lastMousePosition;
    m_lastMousePosition = event->pos();

    m_parameters.cameraAzimuthDeg -= delta.x() * 0.25;
    if (m_parameters.localCamera)
    {
        m_parameters.cameraAzimuthDeg = clamp(m_parameters.cameraAzimuthDeg, -180.0, 180.0);
        m_parameters.cameraPitchDeg = clamp(m_parameters.cameraPitchDeg + delta.y() * 0.20, -180.0, 180.0);
    }
    else
    {
        while (m_parameters.cameraAzimuthDeg < 0.0)
            m_parameters.cameraAzimuthDeg += 360.0;
        while (m_parameters.cameraAzimuthDeg >= 360.0)
            m_parameters.cameraAzimuthDeg -= 360.0;
        m_parameters.cameraPitchDeg = clamp(m_parameters.cameraPitchDeg + delta.y() * 0.20, -89.0, 89.0);
    }

    rebuildPreview();
    update();

    emit cameraChanged(m_parameters.cameraAzimuthDeg, m_parameters.cameraPitchDeg, m_parameters.cameraRollDeg, m_parameters.horizontalFovDeg, m_parameters.verticalFovDeg);
}

// 功能：鼠标滚轮调整垂直视场角。
void SkyPerspectiveWidget::wheelEvent(QWheelEvent* event)
{
    if (m_parameters.useSensorFrameProjection) {
        event->ignore();
        return;
    }

    const double steps = event->angleDelta().y() / 120.0;
    m_parameters.verticalFovDeg = clamp(m_parameters.verticalFovDeg - steps * 5.0, 10.0, 170.0);

    rebuildPreview();
    update();

    emit cameraChanged(m_parameters.cameraAzimuthDeg, m_parameters.cameraPitchDeg, m_parameters.cameraRollDeg, m_parameters.horizontalFovDeg, m_parameters.verticalFovDeg);

    event->accept();
}
