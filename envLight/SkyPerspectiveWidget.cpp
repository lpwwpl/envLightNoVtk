#include "SkyPerspectiveWidget.h"
#include "SkySceneWidget.h"
#include "environment_light.h"

#include <QMouseEvent>
#include <QContextMenuEvent>
#include <QEvent>
#include <QToolTip>
#include <QMenu>
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


// Spectral display: CIE 1931 visible range.
// UV and IR can still be calculated physically, but cannot be mapped to sRGB.
bool isVisibleWavelength(double wavelengthNm)
{
    return wavelengthNm >= 360.0 && wavelengthNm <= 830.0;
}

double naturalAutoExposure(double value, double peakValue)
{
    if (value <= 0.0 || peakValue <= 0.0)
        return 0.0;

    const double normalized = value / peakValue;
    return normalized / (1.0 + normalized);
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

// CIE 1964 10° supplementary standard observer.
// The table below uses the CIE 5 nm abridged values from 380-780 nm and
// linearly interpolates between tabulated wavelengths. Outside the abridged
// range the CMFs are treated as zero for display/integration purposes.
struct CieCmfSample { double wavelengthNm; double x; double y; double z; };

static const CieCmfSample kCie1964_10Deg[] = {
    {380.0, 0.000160, 0.000017, 0.000705},
    {385.0, 0.000662, 0.000072, 0.002928},
    {390.0, 0.002362, 0.000253, 0.010482},
    {395.0, 0.007242, 0.000769, 0.032344},
    {400.0, 0.019110, 0.002004, 0.086011},
    {405.0, 0.043400, 0.004509, 0.197120},
    {410.0, 0.084736, 0.008756, 0.389366},
    {415.0, 0.140638, 0.014456, 0.656760},
    {420.0, 0.204492, 0.021391, 0.972542},
    {425.0, 0.264737, 0.029497, 1.282500},
    {430.0, 0.314679, 0.038676, 1.553480},
    {435.0, 0.357719, 0.049602, 1.798500},
    {440.0, 0.383734, 0.062077, 1.967280},
    {445.0, 0.386726, 0.074704, 2.027300},
    {450.0, 0.370702, 0.089456, 1.994800},
    {455.0, 0.342957, 0.106256, 1.900700},
    {460.0, 0.302273, 0.128201, 1.745370},
    {465.0, 0.254085, 0.152761, 1.554900},
    {470.0, 0.195618, 0.185190, 1.317560},
    {475.0, 0.132349, 0.219940, 1.030200},
    {480.0, 0.080507, 0.253589, 0.772125},
    {485.0, 0.041072, 0.297665, 0.570060},
    {490.0, 0.016172, 0.339133, 0.415254},
    {495.0, 0.005132, 0.395379, 0.302356},
    {500.0, 0.003816, 0.460777, 0.218502},
    {505.0, 0.015444, 0.531360, 0.159249},
    {510.0, 0.037465, 0.606741, 0.112044},
    {515.0, 0.071358, 0.685660, 0.082248},
    {520.0, 0.117749, 0.761757, 0.060709},
    {525.0, 0.172953, 0.823330, 0.043050},
    {530.0, 0.236491, 0.875211, 0.030451},
    {535.0, 0.304213, 0.923810, 0.020584},
    {540.0, 0.376772, 0.961988, 0.013676},
    {545.0, 0.451584, 0.982200, 0.007918},
    {550.0, 0.529826, 0.991761, 0.003988},
    {555.0, 0.616053, 0.999110, 0.001091},
    {560.0, 0.705224, 0.997340, 0.000000},
    {565.0, 0.793832, 0.982380, 0.000000},
    {570.0, 0.878655, 0.955552, 0.000000},
    {575.0, 0.951162, 0.915175, 0.000000},
    {580.0, 1.014160, 0.868934, 0.000000},
    {585.0, 1.074300, 0.825623, 0.000000},
    {590.0, 1.118520, 0.777405, 0.000000},
    {595.0, 1.134300, 0.720353, 0.000000},
    {600.0, 1.123990, 0.658341, 0.000000},
    {605.0, 1.089100, 0.593878, 0.000000},
    {610.0, 1.030480, 0.527963, 0.000000},
    {615.0, 0.950740, 0.461834, 0.000000},
    {620.0, 0.856297, 0.398057, 0.000000},
    {625.0, 0.754930, 0.339554, 0.000000},
    {630.0, 0.647467, 0.283493, 0.000000},
    {635.0, 0.535110, 0.228254, 0.000000},
    {640.0, 0.431567, 0.179828, 0.000000},
    {645.0, 0.343690, 0.140211, 0.000000},
    {650.0, 0.268329, 0.107633, 0.000000},
    {655.0, 0.204300, 0.081187, 0.000000},
    {660.0, 0.152568, 0.060281, 0.000000},
    {665.0, 0.112210, 0.044096, 0.000000},
    {670.0, 0.081261, 0.031800, 0.000000},
    {675.0, 0.057930, 0.022602, 0.000000},
    {680.0, 0.040851, 0.015905, 0.000000},
    {685.0, 0.028623, 0.011130, 0.000000},
    {690.0, 0.019941, 0.007749, 0.000000},
    {695.0, 0.013842, 0.005375, 0.000000},
    {700.0, 0.009577, 0.003718, 0.000000},
    {705.0, 0.006605, 0.002565, 0.000000},
    {710.0, 0.004553, 0.001768, 0.000000},
    {715.0, 0.003145, 0.001222, 0.000000},
    {720.0, 0.002175, 0.000846, 0.000000},
    {725.0, 0.001506, 0.000586, 0.000000},
    {730.0, 0.001045, 0.000407, 0.000000},
    {735.0, 0.000727, 0.000284, 0.000000},
    {740.0, 0.000508, 0.000199, 0.000000},
    {745.0, 0.000356, 0.000140, 0.000000},
    {750.0, 0.000251, 0.000098, 0.000000},
    {755.0, 0.000178, 0.000070, 0.000000},
    {760.0, 0.000126, 0.000050, 0.000000},
    {765.0, 0.000090, 0.000036, 0.000000},
    {770.0, 0.000065, 0.000025, 0.000000},
    {775.0, 0.000046, 0.000018, 0.000000},
    {780.0, 0.000033, 0.000013, 0.000000},
};

QVector3D cieXYZBar1964(double wavelengthNm)
{
    constexpr int count = static_cast<int>(sizeof(kCie1964_10Deg) / sizeof(kCie1964_10Deg[0]));
    if (wavelengthNm < kCie1964_10Deg[0].wavelengthNm || wavelengthNm > kCie1964_10Deg[count - 1].wavelengthNm)
        return QVector3D(0.0f, 0.0f, 0.0f);

    const double position = (wavelengthNm - 380.0) / 5.0;
    const int i0 = std::max(0, std::min(count - 1, static_cast<int>(std::floor(position))));
    const int i1 = std::min(count - 1, i0 + 1);
    if (i0 == i1)
        return QVector3D(static_cast<float>(kCie1964_10Deg[i0].x), static_cast<float>(kCie1964_10Deg[i0].y), static_cast<float>(kCie1964_10Deg[i0].z));

    const double t = std::max(0.0, std::min(1.0, position - static_cast<double>(i0)));
    const auto& a = kCie1964_10Deg[i0];
    const auto& b = kCie1964_10Deg[i1];
    return QVector3D(
        static_cast<float>(a.x + (b.x - a.x) * t),
        static_cast<float>(a.y + (b.y - a.y) * t),
        static_cast<float>(a.z + (b.z - a.z) * t));
}

double cieXBar(double wavelengthNm, SkyObserverType observer)
{
    return observer == SkyObserverType::CIE1964_10Deg ? cieXYZBar1964(wavelengthNm).x() : cieXBar1931(wavelengthNm);
}

double cieYBar(double wavelengthNm, SkyObserverType observer)
{
    return observer == SkyObserverType::CIE1964_10Deg ? cieXYZBar1964(wavelengthNm).y() : cieYBar1931(wavelengthNm);
}

double cieZBar(double wavelengthNm, SkyObserverType observer)
{
    return observer == SkyObserverType::CIE1964_10Deg ? cieXYZBar1964(wavelengthNm).z() : cieZBar1931(wavelengthNm);
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

    // 输入天空亮度是传统 photometric cd/m²，因此 W<->luminance 标定始终使用 CIE 1931 V(λ)=y-bar。
    // Observer 只改变随后计算的 XYZ/色度，不应让 Radiometric/Spectral 的物理瓦特值随观察者切换。
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
        bandX += spectralPower * cieXBar(wavelength, parameters.observerType);
        bandY += spectralPower * cieYBar(wavelength, parameters.observerType);
        bandZ += spectralPower * cieZBar(wavelength, parameters.observerType);
    }

    result.bandRadiancePerLuminance = bandPower / photometricDenominator;

    const double selectedWavelength = std::max(start, std::min(end, parameters.displayWavelengthNm));
    result.spectralRadiancePerLuminance = planckRelative(selectedWavelength, parameters.spectralTemperatureK) / photometricDenominator;

    result.colorimetricYPerLuminance = bandY / std::max(1.0e-18, fullVisibleY);

    result.colorimetricRgb = normalizedLinearSrgbFromXyz(bandX / std::max(1.0e-18, fullVisibleY), bandY / std::max(1.0e-18, fullVisibleY), bandZ / std::max(1.0e-18, fullVisibleY));

    if (isVisibleWavelength(selectedWavelength))
    {
        result.wavelengthRgb = normalizedLinearSrgbFromXyz(
            cieXBar(selectedWavelength, parameters.observerType),
            cieYBar(selectedWavelength, parameters.observerType),
            cieZBar(selectedWavelength, parameters.observerType));
    }
    else
    {
        result.wavelengthRgb = QVector3D(0.0f, 0.0f, 0.0f);
    }

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
        return QString::fromUtf8("Colorimetric Y / chromaticity");
    case SkyMeasurementType::Spectral:
        return QString::fromUtf8("Spectral");
    }

    return QString::fromUtf8("Unknown");
}

} // namespace


// ================================================================
// PerspectiveColorBarWidget
// 独立顶层 Tool 窗口：不占用 PerspectiveWidget 的客户区，可拖到其外部。
// ================================================================
class PerspectiveColorBarWidget final : public QWidget
{
public:
    explicit PerspectiveColorBarWidget(SkyPerspectiveWidget* owner)
        : QWidget(owner, Qt::Tool | Qt::FramelessWindowHint), m_owner(owner)
    {
        setAttribute(Qt::WA_DeleteOnClose, false);
        setMouseTracking(true);
        setFixedSize(141, 323); // 约为上一版 188x430 的 3/4
        setWindowTitle(QStringLiteral("Colorbar"));
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        if (!m_owner || m_owner->m_parameters.colorMode == SkyColorMode::NaturalPreview)
            return;

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(QPen(QColor(105, 105, 105), 1.0));
        painter.setBrush(QColor(235, 235, 235));
        painter.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 3.0, 3.0);

        painter.setPen(QColor(25, 25, 25));
        QFont titleFont = painter.font();
        titleFont.setPointSizeF(std::max(7.0, titleFont.pointSizeF() - 1.0));
        painter.setFont(titleFont);
        painter.drawText(QRect(6, 4, width() - 30, 16), Qt::AlignLeft | Qt::AlignVCenter,
                         m_owner->colorBarTitle());

        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(181, 65, 60));
        painter.drawRect(closeRect());
        painter.setPen(Qt::white);
        painter.drawText(closeRect(), Qt::AlignCenter, QString::fromUtf8("×"));

        const QRect bar = gradientRect();
        for (int yy = 0; yy < bar.height(); ++yy) {
            const double n = 1.0 - static_cast<double>(yy) / std::max(1, bar.height() - 1);
            painter.setPen(m_owner->colorBarColor(n));
            painter.drawLine(bar.left(), bar.top() + yy, bar.right(), bar.top() + yy);
        }
        painter.setPen(QColor(75, 75, 75));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(bar);

        const int tickCount = 10;
        QFont tickFont = painter.font();
        tickFont.setPointSizeF(std::max(6.0, tickFont.pointSizeF() - 2.0));
        painter.setFont(tickFont);
        for (int i = 0; i < tickCount; ++i) {
            const double f = static_cast<double>(i) / (tickCount - 1);
            const int y = bar.top() + static_cast<int>(std::round(f * (bar.height() - 1)));
            const double n = 1.0 - f;
            const double value = m_owner->colorBarValueFromNormalized(n);
            painter.setPen(QColor(50, 50, 50));
            painter.drawLine(bar.right() + 1, y, bar.right() + 6, y);
            painter.drawText(QRect(bar.right() + 8, y - 7, width() - bar.right() - 11, 14),
                             Qt::AlignLeft | Qt::AlignVCenter,
                             m_owner->formatMeasurementValue(value));
        }

        if (m_hoverValid && bar.contains(m_hoverPos)) {
            const int y = m_hoverPos.y();
            painter.setPen(QPen(Qt::white, 2.0));
            painter.drawLine(bar.left(), y, bar.right(), y);
            painter.setPen(QPen(Qt::black, 1.0));
            painter.drawLine(bar.left(), y + 1, bar.right(), y + 1);
            QRect bubble(bar.right() + 6, y - 11, width() - bar.right() - 12, 22);
            if (bubble.top() < bar.top()) bubble.moveTop(bar.top());
            if (bubble.bottom() > bar.bottom()) bubble.moveBottom(bar.bottom());
            painter.setPen(QColor(55, 55, 55));
            painter.setBrush(QColor(255, 255, 225));
            painter.drawRoundedRect(bubble, 2, 2);
            painter.drawText(bubble.adjusted(2, 0, -1, 0), Qt::AlignLeft | Qt::AlignVCenter,
                             m_owner->formatMeasurementValue(m_hoverValue));
        }

        const QRect eye = eyedropperRect();
        painter.setPen(QColor(90, 90, 90));
        painter.setBrush(m_owner->m_eyedropperEnabled ? QColor(210, 225, 245) : QColor(246, 246, 246));
        painter.drawRect(eye);
        painter.drawText(eye, Qt::AlignCenter, QString::fromUtf8("吸管取值"));
        if (m_owner->m_eyedropperValid) {
            painter.setPen(QColor(35, 35, 35));
            painter.drawText(QRect(eye.right() + 4, eye.top(), width() - eye.right() - 7, eye.height()),
                             Qt::AlignLeft | Qt::AlignVCenter,
                             m_owner->formatMeasurementValue(m_owner->m_eyedropperValue));
        }

        const auto drawRadio = [&](const QRect& r, const QString& label, bool checked) {
            painter.setPen(QColor(45, 45, 45));
            painter.setBrush(Qt::NoBrush);
            painter.drawEllipse(QRect(r.left() + 2, r.center().y() - 4, 8, 8));
            if (checked) {
                painter.setBrush(QColor(45, 45, 45));
                painter.drawEllipse(QRect(r.left() + 4, r.center().y() - 1, 3, 3));
            }
            painter.drawText(r.adjusted(13, 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter, label);
        };
        drawRadio(linearRect(), QStringLiteral("Linear"), !m_owner->m_colorBarLogScale);
        drawRadio(logRect(), QStringLiteral("Log"), m_owner->m_colorBarLogScale);
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        if (!m_owner || event->button() != Qt::LeftButton) {
            QWidget::mousePressEvent(event);
            return;
        }
        if (closeRect().contains(event->pos())) {
            m_owner->hideColorBarWindow();
            event->accept();
            return;
        }
        if (eyedropperRect().contains(event->pos())) {
            m_owner->m_eyedropperEnabled = !m_owner->m_eyedropperEnabled;
            if (!m_owner->m_eyedropperEnabled) m_owner->m_eyedropperValid = false;
            m_owner->update();
            update();
            event->accept();
            return;
        }
        if (linearRect().contains(event->pos())) {
            if (m_owner->m_colorBarLogScale) {
                m_owner->m_colorBarLogScale = false;
                m_owner->rebuildPreview();
                m_owner->update();
                update();
            }
            event->accept();
            return;
        }
        if (logRect().contains(event->pos())) {
            if (!m_owner->m_colorBarLogScale) {
                m_owner->m_colorBarLogScale = true;
                m_owner->rebuildPreview();
                m_owner->update();
                update();
            }
            event->accept();
            return;
        }
        if (headerRect().contains(event->pos())) {
            m_dragging = true;
            m_dragOffset = event->globalPos() - frameGeometry().topLeft();
            setCursor(Qt::ClosedHandCursor);
            event->accept();
            return;
        }
        QWidget::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        if (m_dragging && (event->buttons() & Qt::LeftButton)) {
            move(event->globalPos() - m_dragOffset);
            event->accept();
            return;
        }
        const QRect bar = gradientRect();
        const bool hover = bar.contains(event->pos());
        if (hover) {
            const double n = 1.0 - static_cast<double>(event->pos().y() - bar.top()) /
                                      std::max(1, bar.height() - 1);
            m_hoverPos = event->pos();
            m_hoverValue = m_owner->colorBarValueFromNormalized(n);
        }
        if (hover != m_hoverValid || hover) {
            m_hoverValid = hover;
            update();
        }
        event->accept();
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton && m_dragging) {
            m_dragging = false;
            unsetCursor();
            event->accept();
            return;
        }
        QWidget::mouseReleaseEvent(event);
    }

    void leaveEvent(QEvent* event) override
    {
        if (m_hoverValid) {
            m_hoverValid = false;
            update();
        }
        QWidget::leaveEvent(event);
    }

private:
    QRect closeRect() const { return QRect(width() - 19, 5, 13, 13); }
    QRect headerRect() const { return QRect(0, 0, width() - 23, 26); }
    QRect gradientRect() const { return QRect(10, 32, 37, std::max(90, height() - 32 - 59)); }
    QRect eyedropperRect() const { return QRect(8, height() - 50, 58, 17); }
    QRect linearRect() const { return QRect(9, height() - 27, 54, 17); }
    QRect logRect() const { return QRect(70, height() - 27, 47, 17); }

    SkyPerspectiveWidget* m_owner = nullptr;
    bool m_dragging = false;
    QPoint m_dragOffset;
    bool m_hoverValid = false;
    QPoint m_hoverPos;
    double m_hoverValue = 0.0;
};

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
    if (m_colorBarWidget) {
        if (m_parameters.colorMode == SkyColorMode::NaturalPreview) hideColorBarWindow();
        else m_colorBarWidget->update();
    }
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

    if (m_parameters.useSensorFrameProjection)
    {
        QVector3D sensorX = m_parameters.sensorXAxisWorld.normalized();
        QVector3D sensorY = m_parameters.sensorYAxisWorld;
        sensorY -= QVector3D::dotProduct(sensorY, sensorX) * sensorX;
        sensorY = sensorY.lengthSquared() < 1.0e-12f ? QVector3D(0.0f, 0.0f, 1.0f) : sensorY.normalized();
        QVector3D forward = m_parameters.sensorForwardWorld;
        forward -= QVector3D::dotProduct(forward, sensorX) * sensorX;
        forward -= QVector3D::dotProduct(forward, sensorY) * sensorY;
        if (forward.lengthSquared() < 1.0e-12f)
        {
            forward = QVector3D::crossProduct(sensorX, sensorY);
        }
        forward = forward.lengthSquared() < 1.0e-12f ? QVector3D(1.0f, 0.0f, 0.0f) : forward.normalized();
        state.cameraXAxisWorld = sensorX;
        state.cameraYAxisWorld = sensorY;
        state.cameraForwardWorld = forward;
        const double xCoords[4] = {m_parameters.sensorXStartMm, m_parameters.sensorXEndMm, m_parameters.sensorXEndMm, m_parameters.sensorXStartMm};
        const double yCoords[4] = {m_parameters.sensorYEndMm, m_parameters.sensorYEndMm, m_parameters.sensorYStartMm, m_parameters.sensorYStartMm};
        for (int index = 0; index < 4; ++index)
        {
            state.frustumDirectionsWorld[index] = (static_cast<float>(m_parameters.sensorFocalMm) * forward + static_cast<float>(xCoords[index]) * sensorX + static_cast<float>(yCoords[index]) * sensorY).normalized();
        }
        return state;
    }

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

    // Scalar palettes use an explicit Linear/Log scale like Speos. Exposure and gamma
    // remain display controls and are applied after the physical normalization.
    double scaled = m_parameters.exposure * value / referenceValue;
    if (m_colorBarLogScale) {
        // Three-decade logarithmic compression: 0 -> 0, reference -> 1.
        scaled = std::log10(1.0 + 999.0 * std::max(0.0, scaled)) / 3.0;
    }
    scaled = clamp(scaled, 0.0, 1.0);
    return clamp(std::pow(scaled, 1.0 / m_parameters.gamma), 0.0, 1.0);
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
        QVector3D sensorNormal = m_parameters.sensorForwardWorld;
        sensorNormal -= QVector3D::dotProduct(sensorNormal, sensorX) * sensorX;
        sensorNormal -= QVector3D::dotProduct(sensorNormal, sensorY) * sensorY;
        if (sensorNormal.lengthSquared() < 1.0e-12f)
        {
            sensorNormal = QVector3D::crossProduct(sensorX, sensorY);
        }
        sensorNormal = sensorNormal.lengthSquared() < 1.0e-12f ? QVector3D(0.0f, 0.0f, 1.0f) : sensorNormal.normalized();
        double u = (x + 0.5) / safeWidth;
        double v = (y + 0.5) / safeHeight;
        if (m_parameters.sensorXMirror)
        {
            u = 1.0 - u;
        }
        if (m_parameters.sensorYMirror)
        {
            v = 1.0 - v;
        }
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

// Speos-like "Black to white (color)" palette:
// black -> blue -> cyan -> green -> yellow -> red -> white.
QColor SkyPerspectiveWidget::blackToWhiteColor(double value)
{
    const double t = clamp(value, 0.0, 1.0);
    struct Stop { double p; int r; int g; int b; };
    static const Stop stops[] = {
        {0.00,   0,   0,   0},
        {0.10,   0,   0, 150},
        {0.25,   0,  70, 255},
        {0.40,   0, 220, 255},
        {0.55,   0, 235,  60},
        {0.70, 245, 245,   0},
        {0.85, 255,  35,   0},
        {1.00, 255, 255, 255}
    };
    for (size_t i = 1; i < sizeof(stops) / sizeof(stops[0]); ++i) {
        if (t <= stops[i].p) {
            const Stop& a = stops[i - 1];
            const Stop& b = stops[i];
            const double u = (t - a.p) / (b.p - a.p);
            return QColor(
                static_cast<int>(std::lround(a.r + u * (b.r - a.r))),
                static_cast<int>(std::lround(a.g + u * (b.g - a.g))),
                static_cast<int>(std::lround(a.b + u * (b.b - a.b))));
        }
    }
    return QColor(255, 255, 255);
}

QColor SkyPerspectiveWidget::whiteToBlackColor(double value)
{
    return blackToWhiteColor(1.0 - clamp(value, 0.0, 1.0));
}

// Speos-like two-endpoint palette. A smooth RGB interpolation naturally
// passes through violet/magenta between blue (low) and red (high).
QColor SkyPerspectiveWidget::blueToRedColor(double value)
{
    const double t = clamp(value, 0.0, 1.0);
    return QColor::fromRgbF(t, 0.0, 1.0 - t);
}

QColor SkyPerspectiveWidget::redToBlueColor(double value)
{
    return blueToRedColor(1.0 - clamp(value, 0.0, 1.0));
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

    // Colorbar 与当前 Display 的标尺保持一致：Fixed 用 Reference，AutoPeak 用本帧峰值。
    m_lastColorBarMaximum = referenceValue;

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
            const double previewNormalized = naturalAutoExposure(previewDiffuseSamples[y * width + x], std::max(1.0e-12, previewDiffusePeak));
            const double sunCosine = QVector3D::dotProduct(direction, sun);

            QColor color;
            switch (m_parameters.colorMode)
            {
            case SkyColorMode::BlackToWhiteColor:
                color = blackToWhiteColor(normalized);
                break;
            case SkyColorMode::WhiteToBlackColor:
                color = whiteToBlackColor(normalized);
                break;
            case SkyColorMode::BlueToRed:
                color = blueToRedColor(normalized);
                break;
            case SkyColorMode::RedToBlue:
                color = redToBlueColor(normalized);
                break;
            case SkyColorMode::FalseColor:
                color = falseColor(normalized);
                break;
            case SkyColorMode::NaturalPreview:
                // Colorimetric/Spectral 模式显示传感器本身的色度：
                // - Colorimetric：对 Start..End 波段积分后的 XYZ -> sRGB 色度；
                // - Spectral 单波长：该波长的 CIE 1931 XYZ -> sRGB 色度；
                // - Spectral integrated band：使用波段积分色度。
                // Photometric/Radiometric 仍使用原自然天空预览。
                if (m_parameters.measurementType == SkyMeasurementType::Colorimetric ||
                    m_parameters.measurementType == SkyMeasurementType::Spectral)
                {
                    const QVector3D sensorRgb =
                        (m_parameters.measurementType == SkyMeasurementType::Spectral && !m_parameters.spectralDisplayAllWavelengths)
                            ? spectral.wavelengthRgb
                            : spectral.colorimetricRgb;
                    color = chromaticDisplayColor(sensorRgb, normalized);
                }
                else
                {
                    // Natural Preview 是几何/视觉辅助层，不再由 Measurement Layer 把背景清零；Sensor 类型和 Layer 仍保留在 samples 中供数值显示逻辑使用。
                    color = naturalPreviewColor(direction, previewNormalized, sunCosine, directStrength);
                    if (previewSunDisk && sunCosine >= cosSunRadius)
                    {
                        const double warm = clamp(0.90 + 0.10 * directStrength, 0.0, 1.0);
                        color = QColor::fromRgbF(warm, warm * 0.97, warm * 0.86);
                    }
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

    drawEyedropperOverlay(painter);
}

QString SkyPerspectiveWidget::colorBarTitle() const
{
    switch (m_parameters.measurementType) {
    case SkyMeasurementType::Photometric: return QString::fromUtf8("luminance (cd/m²)");
    case SkyMeasurementType::Radiometric: return QString::fromUtf8("radiance (W/(m²·sr))");
    case SkyMeasurementType::Colorimetric: return QString::fromUtf8("colorimetric Y");
    case SkyMeasurementType::Spectral:
        if (m_parameters.spectralDisplayAllWavelengths)
            return QString::fromUtf8("spectral radiance (W/(m²·sr))");
        return QString::fromUtf8("spectral radiance (W/(m²·sr·nm))");
    }
    return QString::fromUtf8("value");
}

QColor SkyPerspectiveWidget::colorBarColor(double normalized) const
{
    normalized = clamp(normalized, 0.0, 1.0);
    switch (m_parameters.colorMode) {
    case SkyColorMode::BlackToWhiteColor: return blackToWhiteColor(normalized);
    case SkyColorMode::WhiteToBlackColor: return whiteToBlackColor(normalized);
    case SkyColorMode::BlueToRed: return blueToRedColor(normalized);
    case SkyColorMode::RedToBlue: return redToBlueColor(normalized);
    case SkyColorMode::FalseColor: return falseColor(normalized);
    case SkyColorMode::GrayscaleLuminance: return QColor::fromRgbF(normalized, normalized, normalized);
    case SkyColorMode::NaturalPreview:
    default: return QColor::fromRgbF(normalized, normalized, normalized);
    }
}

// 功能：反解色条显示位置对应的物理值，保证 hover/tick 与 Linear/Log + Exposure/Gamma 一致。
double SkyPerspectiveWidget::colorBarValueFromNormalized(double normalized) const
{
    normalized = clamp(normalized, 0.0, 1.0);
    const double gamma = std::max(0.1, m_parameters.gamma);
    double scaled = std::pow(normalized, gamma);
    if (m_colorBarLogScale)
        scaled = (std::pow(10.0, 3.0 * scaled) - 1.0) / 999.0;
    const double exposure = std::max(0.001, m_parameters.exposure);
    return std::max(0.0, m_lastColorBarMaximum) * scaled / exposure;
}

QString SkyPerspectiveWidget::formatMeasurementValue(double value) const
{
    const double a = std::abs(value);
    if ((a > 0.0 && a < 1.0e-3) || a >= 1.0e6)
        return QString::number(value, 'e', 3);
    if (a >= 10000.0) return QString::number(value, 'f', 1);
    if (a >= 100.0) return QString::number(value, 'f', 2);
    if (a >= 1.0) return QString::number(value, 'f', 3);
    return QString::number(value, 'g', 5);
}

// 功能：重新计算指定屏幕像素处的物理测量值；用于吸管，不从伪彩色 RGB 反推。
bool SkyPerspectiveWidget::measurementValueAtWidgetPoint(const QPoint& point, double& value) const
{
    if (!rect().contains(point) || width() <= 0 || height() <= 0)
        return false;
    QVector3D direction;
    const QVector3D sphereOriginWorld = m_parameters.useFiniteSkySphere ? cameraOriginWorld() : QVector3D();
    if (!sampleDirectionForPixel(point.x(), point.y(), width(), height(), sphereOriginWorld, direction))
        return false;
    if (worldToSky(direction).z() <= 0.0f)
        return false;

    const double diffuseSourceScale = absoluteScale();
    const SpectralConversion spectral = buildSpectralConversion(m_parameters);
    const bool includeDiffuse = m_parameters.measurementLayer != SkyMeasurementLayer::DirectSunOnly;
    const bool includeDirect = m_parameters.measurementLayer != SkyMeasurementLayer::DiffuseSkyOnly;

    double sourceValue = includeDiffuse ? relativeSkyValue(direction) * diffuseSourceScale : 0.0;
    if (includeDirect) {
        const QVector3D sun = m_parameters.sunDirection.normalized();
        const QVector3D sunLocal = worldToSky(sun);
        if (sunLocal.z() > 0.0f) {
            const double radius = m_parameters.sunAngularRadiusDeg * kDegToRad;
            const double solidAngle = 2.0 * kPi * (1.0 - std::cos(radius));
            const double effectiveDirectNormal = m_parameters.directNormalValue * atmosphericAttenuation();
            if (solidAngle > 1.0e-12 && QVector3D::dotProduct(direction, sun) >= std::cos(radius))
                sourceValue += effectiveDirectNormal / solidAngle;
        }
    }
    value = convertSourceValue(sourceValue, m_parameters, spectral);
    return std::isfinite(value);
}

// 功能：Colorbar 已独立为顶层浮动窗口；PerspectiveWidget 仅绘制吸管十字线和值。
void SkyPerspectiveWidget::drawEyedropperOverlay(QPainter& painter)
{
    if (!m_eyedropperEnabled || !m_eyedropperValid)
        return;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QPoint p = m_eyedropperPos;
    painter.setPen(QPen(Qt::white, 2.0));
    painter.drawLine(p.x() - 8, p.y(), p.x() + 8, p.y());
    painter.drawLine(p.x(), p.y() - 8, p.x(), p.y() + 8);
    painter.setPen(QPen(Qt::black, 1.0));
    painter.drawEllipse(p, 5, 5);
    const QString text = formatMeasurementValue(m_eyedropperValue);
    QRect bubble(p.x() + 12, p.y() - 13, 92, 25);
    if (bubble.right() > width() - 3) bubble.moveRight(p.x() - 12);
    if (bubble.bottom() > height() - 3) bubble.moveBottom(height() - 3);
    painter.setBrush(QColor(255, 255, 225, 235));
    painter.drawRoundedRect(bubble, 3, 3);
    painter.drawText(bubble, Qt::AlignCenter, text);
    painter.restore();
}

void SkyPerspectiveWidget::showColorBarWindow()
{
    if (m_parameters.colorMode == SkyColorMode::NaturalPreview)
        return;

    if (!m_colorBarWidget) {
        m_colorBarWidget = new PerspectiveColorBarWidget(this);
        // 初次显示默认停靠在 PerspectiveWidget 右侧外部；之后用户拖动的位置由窗口自身保留。
        const QPoint outside = mapToGlobal(QPoint(width() + 8, 0));
        m_colorBarWidget->move(outside);
    }
    m_colorBarVisible = true;
    m_colorBarWidget->show();
    m_colorBarWidget->raise();
    m_colorBarWidget->activateWindow();
    m_colorBarWidget->update();
}

void SkyPerspectiveWidget::hideColorBarWindow()
{
    m_colorBarVisible = false;
    if (m_colorBarWidget)
        m_colorBarWidget->hide();
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

// 功能：记录导航相机拖拽起点；Colorbar 内部交互由独立浮动窗口自己处理。
void SkyPerspectiveWidget::mousePressEvent(QMouseEvent* event)
{
    // With eyedropper enabled, clicking the Perspective image pins/updates a physical sample.
    if (event->button() == Qt::LeftButton && m_eyedropperEnabled) {
        double v = 0.0;
        m_eyedropperValid = measurementValueAtWidgetPoint(event->pos(), v);
        if (m_eyedropperValid) {
            m_eyedropperPos = event->pos();
            m_eyedropperValue = v;
        }
        update();
        if (m_colorBarWidget) m_colorBarWidget->update();
        event->accept();
        return;
    }

    if (m_parameters.useSensorFrameProjection) { event->ignore(); return; }
    if (event->button() == Qt::LeftButton) {
        m_lastMousePosition = event->pos();
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

// 功能：右击弹出 Colorbar/吸管/Linear-Log 菜单。
void SkyPerspectiveWidget::contextMenuEvent(QContextMenuEvent* event)
{
    QMenu menu(this);
    const bool scalarPalette = m_parameters.colorMode != SkyColorMode::NaturalPreview;
    QAction* showAction = menu.addAction(tr("显示 Colorbar"));
    showAction->setCheckable(true);
    showAction->setChecked(m_colorBarVisible && m_colorBarWidget && m_colorBarWidget->isVisible());
    showAction->setEnabled(scalarPalette);
    QAction* eyeAction = menu.addAction(tr("吸管取值"));
    eyeAction->setCheckable(true);
    eyeAction->setChecked(m_eyedropperEnabled);
    eyeAction->setEnabled(scalarPalette);
    menu.addSeparator();
    QAction* linearAction = menu.addAction(tr("Linear"));
    linearAction->setCheckable(true);
    linearAction->setChecked(!m_colorBarLogScale);
    linearAction->setEnabled(scalarPalette);
    QAction* logAction = menu.addAction(tr("Log"));
    logAction->setCheckable(true);
    logAction->setChecked(m_colorBarLogScale);
    logAction->setEnabled(scalarPalette);
    if (!scalarPalette) {
        QAction* note = menu.addAction(tr("Natural preview 没有标量 Colorbar"));
        note->setEnabled(false);
    }

    QAction* chosen = menu.exec(event->globalPos());
    if (chosen == showAction) {
        if (showAction->isChecked()) showColorBarWindow();
        else hideColorBarWindow();
    }
    else if (chosen == eyeAction) {
        m_eyedropperEnabled = eyeAction->isChecked();
        if (!m_eyedropperEnabled) m_eyedropperValid = false;
    }
    else if (chosen == linearAction && m_colorBarLogScale) {
        m_colorBarLogScale = false;
        rebuildPreview();
    }
    else if (chosen == logAction && !m_colorBarLogScale) {
        m_colorBarLogScale = true;
        rebuildPreview();
    }
    update();
    if (m_colorBarWidget) m_colorBarWidget->update();
    event->accept();
}

// 功能：处理 Perspective 图像上的吸管 hover 和相机拖拽；Colorbar hover/拖动由独立窗口处理。
void SkyPerspectiveWidget::mouseMoveEvent(QMouseEvent* event)
{
    bool changedOverlay = false;
    if (m_eyedropperEnabled) {
        double v = 0.0;
        const bool valid = measurementValueAtWidgetPoint(event->pos(), v);
        if (valid) {
            m_eyedropperPos = event->pos();
            m_eyedropperValue = v;
        }
        if (valid != m_eyedropperValid || valid) {
            m_eyedropperValid = valid;
            changedOverlay = true;
        }
    }
    if (changedOverlay) {
        update();
        if (m_colorBarWidget) m_colorBarWidget->update();
    }

    if (m_parameters.useSensorFrameProjection) { event->accept(); return; }
    if (!(event->buttons() & Qt::LeftButton)) { event->accept(); return; }

    const QPoint delta = event->pos() - m_lastMousePosition;
    m_lastMousePosition = event->pos();
    m_parameters.cameraAzimuthDeg -= delta.x() * 0.25;
    if (m_parameters.localCamera) {
        m_parameters.cameraAzimuthDeg = clamp(m_parameters.cameraAzimuthDeg, -180.0, 180.0);
        m_parameters.cameraPitchDeg = clamp(m_parameters.cameraPitchDeg + delta.y() * 0.20, -180.0, 180.0);
    } else {
        while (m_parameters.cameraAzimuthDeg < 0.0) m_parameters.cameraAzimuthDeg += 360.0;
        while (m_parameters.cameraAzimuthDeg >= 360.0) m_parameters.cameraAzimuthDeg -= 360.0;
        m_parameters.cameraPitchDeg = clamp(m_parameters.cameraPitchDeg + delta.y() * 0.20, -89.0, 89.0);
    }
    rebuildPreview();
    update();
    emit cameraChanged(m_parameters.cameraAzimuthDeg, m_parameters.cameraPitchDeg,
                       m_parameters.cameraRollDeg, m_parameters.horizontalFovDeg,
                       m_parameters.verticalFovDeg);
}

void SkyPerspectiveWidget::mouseReleaseEvent(QMouseEvent* event)
{
    QWidget::mouseReleaseEvent(event);
}

void SkyPerspectiveWidget::leaveEvent(QEvent* event)
{
    if (m_eyedropperEnabled) {
        m_eyedropperValid = false;
        update();
        if (m_colorBarWidget) m_colorBarWidget->update();
    }
    QWidget::leaveEvent(event);
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
