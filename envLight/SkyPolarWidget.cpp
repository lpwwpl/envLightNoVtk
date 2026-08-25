#include "SkyPolarWidget.h"
#include "SunSky.hpp"

#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <cmath>
#include <algorithm>
#include <limits>
#include <QColor>
#include <array>
namespace {
    constexpr double PI = 3.14159265358979323846;
    constexpr double HALF_PI = PI * 0.5;

    double deg2rad(double d) { return d * PI / 180.0; }
    double rad2deg(double r) { return r * 180.0 / PI; }
    double clamp(double v, double lo, double hi) { return std::max(lo, std::min(v, hi)); }

    SSLib::Vec3f toSunSky(const QVector3D& v) { return { v.x(), v.y(), v.z() }; }
    QVector3D toQt(const SSLib::Vec3f& v) { return { v[0], v[1], v[2] }; }
} // namespace

SkyPolarWidget::SkyPolarWidget(QWidget* parent) : QWidget(parent)
{
    setMinimumSize(600, 600);
    rebuildSubdivision();
    rebuildSkyValues();
}

QSize SkyPolarWidget::minimumSizeHint() const { return QSize(600, 600); }

void SkyPolarWidget::setSubdivisionAngle(double angleDegrees)
{
    if (angleDegrees <= 0.0 || angleDegrees > 30.0 || !std::isfinite(angleDegrees)) return;
    m_subdivisionAngle = angleDegrees;
    rebuildSubdivision();
    rebuildSkyValues();
    update();
}
template <class T>
constexpr const T& clamp(const T& value, const T& low, const T& high) {
    return (value < low) ? low : (high < value) ? high : value;
}
void SkyPolarWidget::setCieSkyType(int type)
{
    type = clamp(type, 0, 14);
    if (m_cieSkyType == type && !m_customMode) return;
    m_cieSkyType = type;
    m_customMode = false;
    rebuildSkyValues();
    update();
}

void SkyPolarWidget::setCustomCoefficients(const SSLib::CIESkyCoefficients& coeffs)
{
    m_customCoeffs = coeffs;
    m_customMode = true;
    rebuildSkyValues();
    update();
}

void SkyPolarWidget::setDiffuseHorizontalIrradiance(double dhi)
{
    if (!std::isfinite(dhi) || dhi < 0.0) return;
    m_dhi = dhi;
    rebuildSkyValues();
    update();
}

void SkyPolarWidget::setSunDirection(const QVector3D& dir)
{
    if (dir.lengthSquared() < 1e-10f) return;
    m_sunDirection = dir.normalized();
    rebuildSkyValues();
    update();
}

void SkyPolarWidget::setLocation(double latitude, double longitude, double timeZone)
{
    m_latitude = latitude;
    m_longitude = longitude;
    m_timeZone = timeZone;
    setDateTime(m_date, m_decimalHour);
}

void SkyPolarWidget::setDateTime(const QDate& date, double decimalHour)
{
    if (!date.isValid() || !std::isfinite(decimalHour)) return;
    m_date = date;
    m_decimalHour = decimalHour;

    const SSLib::Vec3f sun = SSLib::SunDirection(
        static_cast<float>(decimalHour),
        static_cast<float>(m_timeZone),
        date.dayOfYear(),
        static_cast<float>(m_latitude),
        static_cast<float>(m_longitude));
    m_sunDirection = toQt(sun).normalized();

    rebuildSkyValues();
    update();
}

void SkyPolarWidget::setShowSegmentOutlines(bool enabled) { m_showSegmentOutlines = enabled; update(); }
void SkyPolarWidget::setShowSunPaths(bool enabled) { m_showSunPaths = enabled; update(); }
int SkyPolarWidget::segmentCount() const { return m_cells.size(); }
int SkyPolarWidget::bandCount() const { return m_bandCount; }

void SkyPolarWidget::rebuildSubdivision()
{
    m_cells.clear();

    const double bandAngleDeg = m_subdivisionAngle;
    m_bandCount = std::max(1, static_cast<int>(std::round(90.0 / bandAngleDeg)));
    const double actualBandAngle = 90.0 / m_bandCount;

    const int horizonSegs = std::max(1, static_cast<int>(std::round(360.0 / actualBandAngle)));

    const double firstAlt0 = 0.0;
    const double firstAlt1 = deg2rad(actualBandAngle);
    const double firstBandSolidAngle = 2.0 * PI * (std::sin(firstAlt1) - std::sin(firstAlt0));
    const double targetCellSolidAngle = firstBandSolidAngle / horizonSegs;

    for (int band = 0; band < m_bandCount; ++band) {
        const double alt0 = deg2rad(band * actualBandAngle);
        const double alt1 = deg2rad((band + 1) * actualBandAngle);
        const double bandSolidAngle = 2.0 * PI * (std::sin(alt1) - std::sin(alt0));
        int segs = std::max(1, static_cast<int>(std::round(bandSolidAngle / targetCellSolidAngle)));
        const double azStep = 2.0 * PI / segs;
        const double offset = (band % 2 == 0) ? 0.0 : azStep * 0.5;

        for (int seg = 0; seg < segs; ++seg) {
            SkyCell cell;
            cell.altitudeMin = alt0;
            cell.altitudeMax = alt1;
            cell.azimuthMin = seg * azStep + offset;
            cell.azimuthMax = (seg + 1) * azStep + offset;
            cell.solidAngle = azStep * (std::sin(alt1) - std::sin(alt0));
            m_cells.push_back(cell);
        }
    }
}

QVector3D SkyPolarWidget::cellCenterDirection(const SkyCell& cell) const
{
    const double sinAlt = 0.5 * (std::sin(cell.altitudeMin) + std::sin(cell.altitudeMax));
    const double altitude = std::asin(clamp(sinAlt, -1.0, 1.0));
    const double azimuth = 0.5 * (cell.azimuthMin + cell.azimuthMax);
    const double cosAlt = std::cos(altitude);
    return QVector3D(
        static_cast<float>(cosAlt * std::sin(azimuth)),
        static_cast<float>(cosAlt * std::cos(azimuth)),
        static_cast<float>(std::sin(altitude))
    );
}

void SkyPolarWidget::rebuildSkyValues()
{
    if (m_cells.isEmpty()) return;

    const SSLib::Vec3f sun = toSunSky(m_sunDirection.normalized());
    double integral = 0.0;

    // 第一次遍历：计算相对亮度并积分
    for (SkyCell& cell : m_cells) {
        const QVector3D dir = cellCenterDirection(cell);
        const SSLib::Vec3f skyDir = toSunSky(dir);

        double rel;
        if (m_customMode) {
            rel = SSLib::CIECustomSky(m_customCoeffs, skyDir, sun, 1.0f);
        }
        else {
            rel = SSLib::CIEStandardSky(m_cieSkyType, skyDir, sun, 1.0f);
        }
        cell.relativeValue = std::max(0.0, rel);

        integral += cell.relativeValue * std::max(0.0f, dir.z()) * cell.solidAngle;
    }

    double scale = (integral > 1e-12) ? m_dhi / integral : 0.0;

    m_minValue = std::numeric_limits<double>::max();
    m_maxValue = -std::numeric_limits<double>::max();

    for (SkyCell& cell : m_cells) {
        const QVector3D dir = cellCenterDirection(cell);
        const double radiance = cell.relativeValue * scale;
        cell.displayValue = radiance * std::max(0.0f, dir.z()) * cell.solidAngle;
        m_minValue = std::min(m_minValue, cell.displayValue);
        m_maxValue = std::max(m_maxValue, cell.displayValue);
    }

    if (!std::isfinite(m_minValue)) m_minValue = 0.0;
    if (!std::isfinite(m_maxValue) || m_maxValue <= m_minValue) m_maxValue = m_minValue + 1.0;
}

QPointF SkyPolarWidget::projectDirection(const QVector3D& dir, const QPointF& center, double radius) const
{
    const QVector3D n = dir.normalized();
    const double alt = std::asin(clamp(n.z(), -1.0, 1.0));
    const double az = std::atan2(n.x(), n.y());  // +X east, +Y north
    const double r = radius * (HALF_PI - alt) / HALF_PI;
    return QPointF(center.x() + r * std::sin(az), center.y() - r * std::cos(az));
}

QPainterPath SkyPolarWidget::createCellPath(const SkyCell& cell, const QPointF& center, double radius) const
{
    constexpr int arcSamples = 6;
    QPolygonF poly;

    const double outerR = radius * (HALF_PI - cell.altitudeMin) / HALF_PI;
    const double innerR = radius * (HALF_PI - cell.altitudeMax) / HALF_PI;

    for (int i = 0; i <= arcSamples; ++i) {
        double t = double(i) / arcSamples;
        double az = cell.azimuthMin + t * (cell.azimuthMax - cell.azimuthMin);
        poly << QPointF(center.x() + outerR * std::sin(az), center.y() - outerR * std::cos(az));
    }
    for (int i = arcSamples; i >= 0; --i) {
        double t = double(i) / arcSamples;
        double az = cell.azimuthMin + t * (cell.azimuthMax - cell.azimuthMin);
        poly << QPointF(center.x() + innerR * std::sin(az), center.y() - innerR * std::cos(az));
    }

    QPainterPath path;
    path.addPolygon(poly);
    path.closeSubpath();
    return path;
}

QColor SkyPolarWidget::heatMapColor(double normalized) const
{
    normalized = clamp(normalized, 0.0, 1.0);
    struct Stop { double pos; QColor color; };
    static const std::array<Stop, 6> stops{ {
        {0.00, QColor(45, 20, 180)},
        {0.18, QColor(40, 70, 255)},
        {0.38, QColor(0, 210, 255)},
        {0.58, QColor(0, 230, 120)},
        {0.78, QColor(255, 235, 0)},
        {1.00, QColor(255, 30, 0)}
    } };

    for (size_t i = 1; i < stops.size(); ++i) {
        if (normalized <= stops[i].pos) {
            const Stop& L = stops[i - 1];
            const Stop& R = stops[i];
            double t = (R.pos > L.pos) ? (normalized - L.pos) / (R.pos - L.pos) : 0.0;
            return QColor(
                static_cast<int>(L.color.red() + t * (R.color.red() - L.color.red())),
                static_cast<int>(L.color.green() + t * (R.color.green() - L.color.green())),
                static_cast<int>(L.color.blue() + t * (R.color.blue() - L.color.blue()))
            );
        }
    }
    return stops.back().color;
}

void SkyPolarWidget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), palette().window());

    const double margin = 52.0, colorBarWidth = 45.0;
    const double availW = width() - 2 * margin - colorBarWidth;
    const double availH = height() - 2 * margin;
    double side = std::max(10.0, std::min(availW, availH));
    QRectF skyRect(margin, (height() - side) * 0.5, side, side);

    drawSkyCells(painter, skyRect);
    if (m_showSunPaths) drawSunPaths(painter, skyRect);
    drawGrid(painter, skyRect);
    drawAzimuthTicks(painter, skyRect);
    drawSun(painter, skyRect);
    drawColorBar(painter, skyRect);
}

void SkyPolarWidget::drawSkyCells(QPainter& painter, const QRectF& skyRect)
{
    const QPointF center = skyRect.center();
    const double radius = skyRect.width() * 0.5;
    const double range = std::max(1e-12, m_maxValue - m_minValue);

    QPen outlinePen(QColor(50, 50, 50, 150), 0.55);

    for (const SkyCell& cell : m_cells) {
        double norm = (cell.displayValue - m_minValue) / range;
        painter.setBrush(heatMapColor(norm));
        painter.setPen(m_showSegmentOutlines ? outlinePen : Qt::NoPen);
        painter.drawPath(createCellPath(cell, center, radius));
    }
}

void SkyPolarWidget::drawGrid(QPainter& painter, const QRectF& skyRect)
{
    painter.save();
    QPen pen(QColor(30, 30, 30, 190), 0.8);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    const QPointF center = skyRect.center();
    const double radius = skyRect.width() * 0.5;

    painter.drawEllipse(center, radius, radius);

    for (int alt = 10; alt < 90; alt += 10) {
        double r = radius * (90 - alt) / 90.0;
        painter.drawEllipse(center, r, r);
        painter.drawText(QPointF(center.x() + 3, center.y() - r + 12), QString::fromUtf8("%1°").arg(alt));
    }

    painter.drawLine(QPointF(center.x(), center.y() - radius), QPointF(center.x(), center.y() + radius));
    painter.drawLine(QPointF(center.x() - radius, center.y()), QPointF(center.x() + radius, center.y()));
    painter.restore();
}

void SkyPolarWidget::drawAzimuthTicks(QPainter& painter, const QRectF& skyRect)
{
    painter.save();
    const QPointF center = skyRect.center();
    const double radius = skyRect.width() * 0.5;
    painter.setPen(QPen(QColor(20, 20, 20), 0.8));

    for (int deg = 0; deg < 360; deg += 5) {
        double az = deg2rad(deg);
        bool major = (deg % 15 == 0);
        double len = major ? 8.0 : 4.0;
        QPointF p0(center.x() + radius * std::sin(az), center.y() - radius * std::cos(az));
        QPointF p1(center.x() + (radius + len) * std::sin(az), center.y() - (radius + len) * std::cos(az));
        painter.drawLine(p0, p1);
        if (major && deg % 90 != 0) {
            QPointF txt(center.x() + (radius + 20) * std::sin(az), center.y() - (radius + 20) * std::cos(az));
            painter.drawText(QRectF(txt.x() - 20, txt.y() - 10, 40, 20), Qt::AlignCenter, QString::fromUtf8("%1°").arg(deg));
        }
    }

    QFont font = painter.font(); font.setBold(true); font.setPointSizeF(font.pointSizeF() + 2); painter.setFont(font);
    painter.drawText(QRectF(center.x() - 20, center.y() - radius - 35, 40, 25), Qt::AlignCenter, "N");
    painter.drawText(QRectF(center.x() + radius + 8, center.y() - 12, 30, 24), Qt::AlignCenter, "E");
    painter.drawText(QRectF(center.x() - 20, center.y() + radius + 8, 40, 25), Qt::AlignCenter, "S");
    painter.drawText(QRectF(center.x() - radius - 38, center.y() - 12, 30, 24), Qt::AlignCenter, "W");
    painter.restore();
}

void SkyPolarWidget::drawSun(QPainter& painter, const QRectF& skyRect)
{
    if (m_sunDirection.z() <= 0.0f) return;
    QPointF pt = projectDirection(m_sunDirection, skyRect.center(), skyRect.width() * 0.5);
    painter.save();
    painter.setPen(QPen(Qt::white, 1.5));
    painter.setBrush(QColor(255, 45, 10));
    painter.drawEllipse(pt, 6.0, 6.0);
    painter.restore();
}

void SkyPolarWidget::drawSunPaths(QPainter& painter, const QRectF& skyRect)
{
    const QPointF center = skyRect.center();
    const double radius = skyRect.width() * 0.5;
    painter.save();

    // 每月 21 日轨迹
    QPen pathPen(QColor(255, 70, 20, 210), 1.0);
    painter.setPen(pathPen);
    painter.setBrush(Qt::NoBrush);

    for (int month = 1; month <= 12; ++month) {
        QDate date(m_date.year(), month, 21);
        if (!date.isValid()) continue;
        QPainterPath path;
        bool started = false;
        for (int min = 0; min <= 24 * 60; min += 10) {
            double t = min / 60.0;
            SSLib::Vec3f sun = SSLib::SunDirection(t, m_timeZone, date.dayOfYear(),
                m_latitude, m_longitude);
            QVector3D dir = toQt(sun);
            if (dir.z() <= 0.0f) { started = false; continue; }
            QPointF pt = projectDirection(dir, center, radius);
            if (!started) { path.moveTo(pt); started = true; }
            else path.lineTo(pt);
        }
        painter.drawPath(path);
    }

    // 小时线
    QPen hourPen(QColor(255, 130, 30, 180), 0.8, Qt::DotLine);
    painter.setPen(hourPen);
    for (int hour = 6; hour <= 18; ++hour) {
        QPainterPath path;
        bool started = false;
        for (int day = 1; day <= 365; day += 4) {
            QDate date(m_date.year(), 1, 1);
            date = date.addDays(day - 1);
            SSLib::Vec3f sun = SSLib::SunDirection(hour, m_timeZone, date.dayOfYear(),
                m_latitude, m_longitude);
            QVector3D dir = toQt(sun);
            if (dir.z() <= 0.0f) { started = false; continue; }
            QPointF pt = projectDirection(dir, center, radius);
            if (!started) { path.moveTo(pt); started = true; }
            else path.lineTo(pt);
        }
        painter.drawPath(path);
    }
    painter.restore();
}

void SkyPolarWidget::drawColorBar(QPainter& painter, const QRectF& skyRect)
{
    painter.save();
    const QRectF barRect(skyRect.right() + 25, skyRect.top(), 15, 120);
    QLinearGradient grad(barRect.bottomLeft(), barRect.topLeft());
    const int steps = 100;
    for (int i = 0; i <= steps; ++i) {
        double t = double(i) / steps;
        grad.setColorAt(t, heatMapColor(t));
    }
    painter.setPen(QPen(QColor(60, 60, 60), 0.8));
    painter.setBrush(grad);
    painter.drawRect(barRect);

    painter.setPen(palette().text().color());
    painter.drawText(QRectF(barRect.left() - 5, barRect.top() - 25, 50, 20), Qt::AlignLeft, "W/m²");
    painter.drawText(QRectF(barRect.right() + 5, barRect.top() - 8, 70, 20), Qt::AlignLeft, QString::number(m_maxValue, 'f', 3));
    painter.drawText(QRectF(barRect.right() + 5, barRect.bottom() - 10, 70, 20), Qt::AlignLeft, QString::number(m_minValue, 'f', 3));
    painter.restore();
}