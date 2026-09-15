#include "SkyPolarWidget.h"

#include <QColor>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kHalfPi = kPi * 0.5;

// ================================================================
// 极坐标图内部辅助类型
// ================================================================
struct HeatMapStop {
    double position = 0.0;
    QColor color;
};

// 功能：把角度从度转换为弧度。
double degreesToRadians(double degrees)
{
    return degrees * kPi / 180.0;
}

// 功能：把数值限制到给定闭区间。
double clampValue(double value, double minimum, double maximum)
{
    return std::max(minimum, std::min(value, maximum));
}

// 功能：把 QVector3D 转换成 SunSky 使用的三维向量类型。
SSLib::Vec3f toSunSkyVector(const QVector3D& vector)
{
    return SSLib::Vec3f(vector.x(), vector.y(), vector.z());
}

// 功能：把 SunSky 使用的三维向量转换成 QVector3D。
QVector3D toQtVector(const SSLib::Vec3f& vector)
{
    return QVector3D(vector[0], vector[1], vector[2]);
}

// 功能：把经度规范到 [-180, 180) 区间，避免跨日期线时数值持续累积。
double wrapLongitude(double longitude)
{
    if (!std::isfinite(longitude)) {
        return 0.0;
    }

    double wrapped = std::fmod(longitude + 180.0, 360.0);
    if (wrapped < 0.0) {
        wrapped += 360.0;
    }
    return wrapped - 180.0;
}

} // namespace

// 功能：创建天空极坐标图并建立默认半球离散网格。
SkyPolarWidget::SkyPolarWidget(QWidget* parent) : QWidget(parent)
{
    setMinimumSize(600, 600);
    rebuildSubdivision();
    rebuildSkyValues();
}

// 功能：返回控件建议的最小尺寸。
QSize SkyPolarWidget::minimumSizeHint() const
{
    return QSize(600, 600);
}

// 功能：设置高度方向的目标分割角度并重新构建近似等立体角网格。
void SkyPolarWidget::setSubdivisionAngle(double angleDegrees)
{
    if (!std::isfinite(angleDegrees) || angleDegrees <= 0.0 || angleDegrees > 30.0) {
        return;
    }

    if (std::abs(m_subdivisionAngle - angleDegrees) <= 1.0e-9) {
        return;
    }

    m_subdivisionAngle = angleDegrees;
    rebuildSubdivision();
    rebuildSkyValues();
    update();
}

// 功能：设置 0~14 的 CIE 标准天空类型。
void SkyPolarWidget::setCieSkyType(int type)
{
    const int boundedType = std::max(0, std::min(type, 14));
    if (m_cieSkyType == boundedType && !m_customMode) {
        return;
    }

    m_cieSkyType = boundedType;
    m_customMode = false;
    rebuildSkyValues();
    update();
}

// 功能：切换为自定义 CIE 系数并重新计算天空分布。
void SkyPolarWidget::setCustomCoefficients(const SSLib::CIESkyCoefficients& coefficients)
{
    m_customCoeffs = coefficients;
    m_customMode = true;
    rebuildSkyValues();
    update();
}

// 功能：设置水平散射辐照度 DHI，单位 W/m²。
void SkyPolarWidget::setDiffuseHorizontalIrradiance(double dhi)
{
    if (!std::isfinite(dhi) || dhi < 0.0) {
        return;
    }

    if (std::abs(m_dhi - dhi) <= 1.0e-12) {
        return;
    }

    m_dhi = dhi;
    rebuildSkyValues();
    update();
}

// 功能：直接设置 ENU 中的太阳单位方向。
void SkyPolarWidget::setSunDirection(const QVector3D& direction)
{
    if (!std::isfinite(direction.x()) || !std::isfinite(direction.y()) || !std::isfinite(direction.z()) || direction.lengthSquared() < 1.0e-12f) {
        return;
    }

    m_sunDirection = direction.normalized();
    rebuildSkyValues();
    update();
}

// 功能：设置太阳轨迹计算使用的纬度、经度和时区。
void SkyPolarWidget::setLocation(double latitude, double longitude, double timeZone)
{
    if (!std::isfinite(latitude) || !std::isfinite(longitude) || !std::isfinite(timeZone)) {
        return;
    }

    m_latitude = clampValue(latitude, -90.0, 90.0);
    m_longitude = wrapLongitude(longitude);
    m_timeZone = clampValue(timeZone, -14.0, 14.0);
    setDateTime(m_date, m_decimalHour);
}

// 功能：设置日期与当地小数小时，并据此刷新太阳方向。
void SkyPolarWidget::setDateTime(const QDate& date, double decimalHour)
{
    if (!date.isValid() || !std::isfinite(decimalHour)) {
        return;
    }

    m_date = date;
    m_decimalHour = clampValue(decimalHour, 0.0, 24.0);

    const SSLib::Vec3f sun = SSLib::SunDirection(static_cast<float>(m_decimalHour), static_cast<float>(m_timeZone), m_date.dayOfYear(), static_cast<float>(m_latitude), static_cast<float>(m_longitude));

    const QVector3D sunDirection = toQtVector(sun);
    if (sunDirection.lengthSquared() > 1.0e-12f) {
        m_sunDirection = sunDirection.normalized();
    }

    rebuildSkyValues();
    update();
}

// 功能：控制天空离散单元边界是否显示。
void SkyPolarWidget::setShowSegmentOutlines(bool enabled)
{
    if (m_showSegmentOutlines == enabled) {
        return;
    }

    m_showSegmentOutlines = enabled;
    update();
}

// 功能：控制全年太阳轨迹辅助线是否显示。
void SkyPolarWidget::setShowSunPaths(bool enabled)
{
    if (m_showSunPaths == enabled) {
        return;
    }

    m_showSunPaths = enabled;
    update();
}

// 功能：返回当前天空离散单元数量。
int SkyPolarWidget::segmentCount() const
{
    return m_cells.size();
}

// 功能：返回当前高度带数量。
int SkyPolarWidget::bandCount() const
{
    return m_bandCount;
}

// 功能：按照目标角度重建半球离散单元。
//
// 球面不能按普通二维面积均匀切分。高度带 [h1,h2] 的立体角为
//   Ωband = 2*pi*(sin(h2)-sin(h1))。
// 因此这里根据每个高度带的 Ω 调整方位分段数，使单元 dΩ 尽量接近，
// 后续做 CIE 天空积分时不会让地平线附近因为“格子多”而被错误加权。
void SkyPolarWidget::rebuildSubdivision()
{
    m_cells.clear();

    m_bandCount = std::max(1, static_cast<int>(std::round(90.0 / m_subdivisionAngle)));
    const double actualBandAngle = 90.0 / static_cast<double>(m_bandCount);
    const int horizonSegments = std::max(1, static_cast<int>(std::round(360.0 / actualBandAngle)));

    const double firstAltitudeMax = degreesToRadians(actualBandAngle);
    const double firstBandSolidAngle = 2.0 * kPi * std::sin(firstAltitudeMax);
    const double targetCellSolidAngle = firstBandSolidAngle / static_cast<double>(horizonSegments);

    for (int band = 0; band < m_bandCount; ++band)
    {
        const double altitudeMin = degreesToRadians(band * actualBandAngle);
        const double altitudeMax = degreesToRadians((band + 1) * actualBandAngle);
        const double bandSolidAngle = 2.0 * kPi * (std::sin(altitudeMax) - std::sin(altitudeMin));

        const int segmentCount = std::max(1, static_cast<int>(std::round(bandSolidAngle / targetCellSolidAngle)));
        const double azimuthStep = 2.0 * kPi / static_cast<double>(segmentCount);
        const double azimuthOffset = (band % 2 == 0) ? 0.0 : azimuthStep * 0.5;

        for (int segment = 0; segment < segmentCount; ++segment)
        {
            SkyCell cell;
            cell.altitudeMin = altitudeMin;
            cell.altitudeMax = altitudeMax;
            cell.azimuthMin = segment * azimuthStep + azimuthOffset;
            cell.azimuthMax = (segment + 1) * azimuthStep + azimuthOffset;
            // 单元立体角 dΩ = dAz * (sin(h2)-sin(h1))。
            cell.solidAngle = azimuthStep * (std::sin(altitudeMax) - std::sin(altitudeMin));
            m_cells.push_back(cell);
        }
    }
}

// 功能：计算单元中心对应的 ENU 单位方向。
// 使用平均 sin(altitude) 选代表方向，比直接取高度角算术平均更符合球面面积权重。
QVector3D SkyPolarWidget::cellCenterDirection(const SkyCell& cell) const
{
    const double meanSinAltitude = 0.5 * (std::sin(cell.altitudeMin) + std::sin(cell.altitudeMax));
    const double altitude = std::asin(clampValue(meanSinAltitude, -1.0, 1.0));
    const double azimuth = 0.5 * (cell.azimuthMin + cell.azimuthMax);
    const double cosAltitude = std::cos(altitude);

    return QVector3D(static_cast<float>(cosAltitude * std::sin(azimuth)), static_cast<float>(cosAltitude * std::cos(azimuth)), static_cast<float>(std::sin(altitude)));
}

// 功能：计算各单元相对亮度，并按 DHI 完成绝对标定。
//
// CIEStandardSky/CIECustomSky 返回的是相对亮度 Lrel(P)：
//   z=当前方向到天顶的角度，chi=当前方向到太阳的球面夹角，
//   不同 CIE Type 通过 a,b,c,d,e 系数改变地平线梯度和太阳周围散射。
// 相对分布本身没有 W/m² 量纲，因此先求
//   Irel = Σ Lrel(Pi) * cos(theta_i) * dΩ_i
// 再令 scale=DHI/Irel，使 Σ(Lrel*scale*cos(theta)*dΩ)=DHI。
void SkyPolarWidget::rebuildSkyValues()
{
    if (m_cells.isEmpty()) {
        m_minValue = 0.0;
        m_maxValue = 1.0;
        return;
    }

    QVector3D normalizedSun = m_sunDirection;
    if (normalizedSun.lengthSquared() < 1.0e-12f) {
        normalizedSun = QVector3D(0.0f, 1.0f, 0.0f);
    }
    normalizedSun.normalize();

    // CIE 物理层固定使用 ENU：+Z 为 Up，所以方向 z 分量就是 cos(theta)。
    const SSLib::Vec3f sun = toSunSkyVector(normalizedSun);
    double projectedIntegral = 0.0;

    for (SkyCell& cell : m_cells)
    {
        const QVector3D direction = cellCenterDirection(cell);
        const SSLib::Vec3f skyDirection = toSunSkyVector(direction);

        double relativeValue = 0.0;
        if (m_customMode) {
            relativeValue = SSLib::CIECustomSky(m_customCoeffs, skyDirection, sun, 1.0f);
        } else {
            relativeValue = SSLib::CIEStandardSky(m_cieSkyType, skyDirection, sun, 1.0f);
        }

        cell.relativeValue = std::max(0.0, relativeValue);

        // 水平面投影权重 cos(theta)=direction.z()；再乘单元立体角 dΩ。
        // 这一项是该单元对 DHI 积分的“相对贡献”。
        projectedIntegral += cell.relativeValue * std::max(0.0, static_cast<double>(direction.z())) * cell.solidAngle;
    }

    // 一个全局 scale 保持 CIE 方向形状不变，只把整个天空缩放到目标 DHI。
    const double absoluteScale = (projectedIntegral > 1.0e-12) ? (m_dhi / projectedIntegral) : 0.0;

    m_minValue = std::numeric_limits<double>::max();
    m_maxValue = -std::numeric_limits<double>::max();

    for (SkyCell& cell : m_cells)
    {
        const QVector3D direction = cellCenterDirection(cell);
        const double radiance = cell.relativeValue * absoluteScale;
        // 热力图显示“该单元对水平 DHI 的贡献”，而不是单纯显示方向 radiance。
        cell.displayValue = radiance * std::max(0.0, static_cast<double>(direction.z())) * cell.solidAngle;

        m_minValue = std::min(m_minValue, cell.displayValue);
        m_maxValue = std::max(m_maxValue, cell.displayValue);
    }

    if (!std::isfinite(m_minValue)) {
        m_minValue = 0.0;
    }
    if (!std::isfinite(m_maxValue) || m_maxValue <= m_minValue) {
        m_maxValue = m_minValue + 1.0;
    }
}

// 功能：把上半球方向投影为极坐标图中的二维点。
QPointF SkyPolarWidget::projectDirection(const QVector3D& direction, const QPointF& center, double radius) const
{
    QVector3D normalized = direction;
    if (normalized.lengthSquared() < 1.0e-12f) {
        normalized = QVector3D(0.0f, 1.0f, 0.0f);
    }
    normalized.normalize();

    const double altitude = std::asin(clampValue(normalized.z(), -1.0, 1.0));
    const double azimuth = std::atan2(normalized.x(), normalized.y());
    const double radialDistance = radius * (kHalfPi - altitude) / kHalfPi;

    return QPointF(center.x() + radialDistance * std::sin(azimuth), center.y() - radialDistance * std::cos(azimuth));
}

// 功能：创建一个天空离散单元的二维扇环路径。
QPainterPath SkyPolarWidget::createCellPath(const SkyCell& cell, const QPointF& center, double radius) const
{
    constexpr int kArcSamples = 6;
    QPolygonF polygon;

    const double outerRadius = radius * (kHalfPi - cell.altitudeMin) / kHalfPi;
    const double innerRadius = radius * (kHalfPi - cell.altitudeMax) / kHalfPi;

    for (int index = 0; index <= kArcSamples; ++index)
    {
        const double interpolation = static_cast<double>(index) / kArcSamples;
        const double azimuth = cell.azimuthMin + interpolation * (cell.azimuthMax - cell.azimuthMin);
        polygon << QPointF(center.x() + outerRadius * std::sin(azimuth), center.y() - outerRadius * std::cos(azimuth));
    }

    for (int index = kArcSamples; index >= 0; --index)
    {
        const double interpolation = static_cast<double>(index) / kArcSamples;
        const double azimuth = cell.azimuthMin + interpolation * (cell.azimuthMax - cell.azimuthMin);
        polygon << QPointF(center.x() + innerRadius * std::sin(azimuth), center.y() - innerRadius * std::cos(azimuth));
    }

    QPainterPath path;
    path.addPolygon(polygon);
    path.closeSubpath();
    return path;
}

// 功能：把 0~1 归一化数值映射成热力图颜色。
QColor SkyPolarWidget::heatMapColor(double normalized) const
{
    static const std::array<HeatMapStop, 6> kStops = {{
        {0.00, QColor(45, 20, 180)},
        {0.18, QColor(40, 70, 255)},
        {0.38, QColor(0, 210, 255)},
        {0.58, QColor(0, 230, 120)},
        {0.78, QColor(255, 235, 0)},
        {1.00, QColor(255, 30, 0)}
    }};

    const double bounded = clampValue(normalized, 0.0, 1.0);
    for (std::size_t index = 1; index < kStops.size(); ++index)
    {
        if (bounded > kStops[index].position) {
            continue;
        }

        const HeatMapStop& left = kStops[index - 1];
        const HeatMapStop& right = kStops[index];
        const double span = right.position - left.position;
        const double interpolation = (span > 1.0e-12) ? (bounded - left.position) / span : 0.0;

        return QColor(static_cast<int>(left.color.red() + interpolation * (right.color.red() - left.color.red())), static_cast<int>(left.color.green() + interpolation * (right.color.green() - left.color.green())), static_cast<int>(left.color.blue() + interpolation * (right.color.blue() - left.color.blue())));
    }

    return kStops.back().color;
}

// 功能：绘制天空单元、网格、方位刻度、太阳和色标。
void SkyPolarWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), palette().window());

    constexpr double kMargin = 52.0;
    constexpr double kColorBarWidth = 45.0;
    const double availableWidth = width() - 2.0 * kMargin - kColorBarWidth;
    const double availableHeight = height() - 2.0 * kMargin;
    const double side = std::max(10.0, std::min(availableWidth, availableHeight));
    const QRectF skyRect(kMargin, (height() - side) * 0.5, side, side);

    drawSkyCells(painter, skyRect);
    if (m_showSunPaths) {
        drawSunPaths(painter, skyRect);
    }
    drawGrid(painter, skyRect);
    drawAzimuthTicks(painter, skyRect);
    drawSun(painter, skyRect);
    drawColorBar(painter, skyRect);
}

// 功能：绘制按绝对辐照度贡献着色的天空离散单元。
void SkyPolarWidget::drawSkyCells(QPainter& painter, const QRectF& skyRect)
{
    const QPointF center = skyRect.center();
    const double radius = skyRect.width() * 0.5;
    const double range = std::max(1.0e-12, m_maxValue - m_minValue);
    const QPen outlinePen(QColor(50, 50, 50, 150), 0.55);

    for (const SkyCell& cell : m_cells)
    {
        const double normalized = (cell.displayValue - m_minValue) / range;
        painter.setBrush(heatMapColor(normalized));
        painter.setPen(m_showSegmentOutlines ? outlinePen : QPen(Qt::NoPen));
        painter.drawPath(createCellPath(cell, center, radius));
    }
}

// 功能：绘制高度圈和南北/东西参考线。
void SkyPolarWidget::drawGrid(QPainter& painter, const QRectF& skyRect)
{
    painter.save();
    painter.setPen(QPen(QColor(30, 30, 30, 190), 0.8));
    painter.setBrush(Qt::NoBrush);

    const QPointF center = skyRect.center();
    const double radius = skyRect.width() * 0.5;
    painter.drawEllipse(center, radius, radius);

    for (int altitude = 10; altitude < 90; altitude += 10)
    {
        const double ringRadius = radius * (90.0 - altitude) / 90.0;
        painter.drawEllipse(center, ringRadius, ringRadius);
        painter.drawText(QPointF(center.x() + 3.0, center.y() - ringRadius + 12.0), QString::fromUtf8("%1°").arg(altitude));
    }

    painter.drawLine(QPointF(center.x(), center.y() - radius), QPointF(center.x(), center.y() + radius));
    painter.drawLine(QPointF(center.x() - radius, center.y()), QPointF(center.x() + radius, center.y()));
    painter.restore();
}

// 功能：绘制 0~360° 方位刻度及 N/E/S/W 标识。
void SkyPolarWidget::drawAzimuthTicks(QPainter& painter, const QRectF& skyRect)
{
    painter.save();

    const QPointF center = skyRect.center();
    const double radius = skyRect.width() * 0.5;
    painter.setPen(QPen(QColor(20, 20, 20), 0.8));

    // 每 5° 绘制一个方位刻度；15° 的倍数使用长刻度，90° 主方向由 N/E/S/W 单独标识。
    for (int degree = 0; degree < 360; degree += 5)
    {
        const double azimuth = degreesToRadians(degree);
        const bool majorTick = (degree % 15 == 0);
        const double tickLength = majorTick ? 8.0 : 4.0;

        // 极坐标图约定 0° 指向北方，因此屏幕 X 使用 sin，屏幕 Y 使用 -cos。
        const QPointF innerPoint(center.x() + radius * std::sin(azimuth), center.y() - radius * std::cos(azimuth));
        const QPointF outerPoint(center.x() + (radius + tickLength) * std::sin(azimuth), center.y() - (radius + tickLength) * std::cos(azimuth));
        painter.drawLine(innerPoint, outerPoint);

        // 非四个主方向的长刻度直接显示角度，避免与 N/E/S/W 标签发生重叠。
        if (majorTick && degree % 90 != 0) {
            const QPointF textPoint(center.x() + (radius + 20.0) * std::sin(azimuth), center.y() - (radius + 20.0) * std::cos(azimuth));
            painter.drawText(QRectF(textPoint.x() - 20.0, textPoint.y() - 10.0, 40.0, 20.0), Qt::AlignCenter, QString::fromUtf8("%1°").arg(degree));
        }
    }

    QFont font = painter.font();
    font.setBold(true);
    font.setPointSizeF(font.pointSizeF() + 2.0);
    painter.setFont(font);

    painter.drawText(QRectF(center.x() - 20.0, center.y() - radius - 35.0, 40.0, 25.0), Qt::AlignCenter, "N");
    painter.drawText(QRectF(center.x() + radius + 8.0, center.y() - 12.0, 30.0, 24.0), Qt::AlignCenter, "E");
    painter.drawText(QRectF(center.x() - 20.0, center.y() + radius + 8.0, 40.0, 25.0), Qt::AlignCenter, "S");
    painter.drawText(QRectF(center.x() - radius - 38.0, center.y() - 12.0, 30.0, 24.0), Qt::AlignCenter, "W");

    painter.restore();
}

// 功能：绘制当前太阳位置。
void SkyPolarWidget::drawSun(QPainter& painter, const QRectF& skyRect)
{
    if (m_sunDirection.z() <= 0.0f) {
        return;
    }

    const QPointF point = projectDirection(m_sunDirection, skyRect.center(), skyRect.width() * 0.5);

    painter.save();
    painter.setPen(QPen(Qt::white, 1.5));
    painter.setBrush(QColor(255, 45, 10));
    painter.drawEllipse(point, 6.0, 6.0);
    painter.restore();
}

// 功能：绘制每月典型日轨迹与整年小时线。
void SkyPolarWidget::drawSunPaths(QPainter& painter, const QRectF& skyRect)
{
    const QPointF center = skyRect.center();
    const double radius = skyRect.width() * 0.5;
    painter.save();

    painter.setPen(QPen(QColor(255, 70, 20, 210), 1.0));
    painter.setBrush(Qt::NoBrush);

    for (int month = 1; month <= 12; ++month)
    {
        const QDate typicalDate(m_date.year(), month, 21);
        if (!typicalDate.isValid()) {
            continue;
        }

        QPainterPath path;
        bool pathStarted = false;
        for (int minute = 0; minute <= 24 * 60; minute += 10)
        {
            const double decimalHour = minute / 60.0;
            const SSLib::Vec3f sun = SSLib::SunDirection(static_cast<float>(decimalHour), static_cast<float>(m_timeZone), typicalDate.dayOfYear(), static_cast<float>(m_latitude), static_cast<float>(m_longitude));
            const QVector3D direction = toQtVector(sun);

            if (direction.z() <= 0.0f) {
                pathStarted = false;
                continue;
            }

            const QPointF point = projectDirection(direction, center, radius);
            if (!pathStarted) {
                path.moveTo(point);
                pathStarted = true;
            } else {
                path.lineTo(point);
            }
        }
        painter.drawPath(path);
    }

    painter.setPen(QPen(QColor(255, 130, 30, 180), 0.8, Qt::DotLine));
    const int daysInYear = QDate(m_date.year(), 12, 31).dayOfYear();
    for (int hour = 6; hour <= 18; ++hour)
    {
        QPainterPath path;
        bool pathStarted = false;

        for (int day = 1; day <= daysInYear; day += 4)
        {
            const QDate date = QDate(m_date.year(), 1, 1).addDays(day - 1);
            const SSLib::Vec3f sun = SSLib::SunDirection(static_cast<float>(hour), static_cast<float>(m_timeZone), date.dayOfYear(), static_cast<float>(m_latitude), static_cast<float>(m_longitude));
            const QVector3D direction = toQtVector(sun);

            if (direction.z() <= 0.0f) {
                pathStarted = false;
                continue;
            }

            const QPointF point = projectDirection(direction, center, radius);
            if (!pathStarted) {
                path.moveTo(point);
                pathStarted = true;
            } else {
                path.lineTo(point);
            }
        }
        painter.drawPath(path);
    }

    painter.restore();
}

// 功能：绘制天空单元辐照度贡献色标。
void SkyPolarWidget::drawColorBar(QPainter& painter, const QRectF& skyRect)
{
    painter.save();

    const QRectF barRect(skyRect.right() + 25.0, skyRect.top(), 15.0, 120.0);
    QLinearGradient gradient(barRect.bottomLeft(), barRect.topLeft());
    constexpr int kGradientSteps = 100;
    for (int index = 0; index <= kGradientSteps; ++index)
    {
        const double position = static_cast<double>(index) / kGradientSteps;
        gradient.setColorAt(position, heatMapColor(position));
    }

    painter.setPen(QPen(QColor(60, 60, 60), 0.8));
    painter.setBrush(gradient);
    painter.drawRect(barRect);

    painter.setPen(palette().text().color());
    painter.drawText(QRectF(barRect.left() - 5.0, barRect.top() - 25.0, 80.0, 20.0), Qt::AlignLeft, "Cell W/m²");
    painter.drawText(QRectF(barRect.right() + 5.0, barRect.top() - 8.0, 90.0, 20.0), Qt::AlignLeft, QString::number(m_maxValue, 'f', 3));
    painter.drawText(QRectF(barRect.right() + 5.0, barRect.bottom() - 10.0, 90.0, 20.0), Qt::AlignLeft, QString::number(m_minValue, 'f', 3));

    painter.restore();
}
