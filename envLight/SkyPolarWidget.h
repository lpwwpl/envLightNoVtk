#ifndef SKYPOLARWIDGET_H
#define SKYPOLARWIDGET_H

#include <QWidget>
#include <QDate>
#include <QVector>
#include <QVector3D>
#include "SunSky.hpp"

struct SkyCell {
    double altitudeMin = 0.0;
    double altitudeMax = 0.0;
    double azimuthMin = 0.0;
    double azimuthMax = 0.0;
    double solidAngle = 0.0;
    double relativeValue = 0.0;
    double displayValue = 0.0;
};

class SkyPolarWidget final : public QWidget {
    Q_OBJECT
public:
    explicit SkyPolarWidget(QWidget* parent = nullptr);

    void setSubdivisionAngle(double angleDegrees);
    void setCieSkyType(int type);                     // 0-14
    void setCustomCoefficients(const SSLib::CIESkyCoefficients& coeffs);
    void setDiffuseHorizontalIrradiance(double dhi);
    void setSunDirection(const QVector3D& direction);
    void setLocation(double latitude, double longitude, double timeZone);
    void setDateTime(const QDate& date, double decimalHour);
    void setShowSegmentOutlines(bool enabled);
    void setShowSunPaths(bool enabled);

    int segmentCount() const;
    int bandCount() const;

protected:
    void paintEvent(QPaintEvent* event) override;
    QSize minimumSizeHint() const override;

private:
    void rebuildSubdivision();
    void rebuildSkyValues();

    QVector3D cellCenterDirection(const SkyCell& cell) const;
    QPointF projectDirection(const QVector3D& direction, const QPointF& center, double radius) const;
    QPainterPath createCellPath(const SkyCell& cell, const QPointF& center, double radius) const;
    QColor heatMapColor(double normalized) const;

    void drawSkyCells(QPainter& painter, const QRectF& skyRect);
    void drawGrid(QPainter& painter, const QRectF& skyRect);
    void drawAzimuthTicks(QPainter& painter, const QRectF& skyRect);
    void drawSun(QPainter& painter, const QRectF& skyRect);
    void drawSunPaths(QPainter& painter, const QRectF& skyRect);
    void drawColorBar(QPainter& painter, const QRectF& skyRect);

private:
    QVector<SkyCell> m_cells;

    double m_subdivisionAngle = 5.0;
    double m_dhi = 100.0;

    int m_cieSkyType = 11;               // 0-based type 12
    bool m_customMode = false;
    SSLib::CIESkyCoefficients m_customCoeffs;

    QVector3D m_sunDirection{ 0.5f, -0.5f, 0.7071f };

    double m_latitude = 39.9;
    double m_longitude = 116.4;
    double m_timeZone = 8.0;

    QDate m_date = QDate(2018, 4, 21);
    double m_decimalHour = 10.5;

    bool m_showSegmentOutlines = true;
    bool m_showSunPaths = true;

    int m_bandCount = 18;

    double m_minValue = 0.0;
    double m_maxValue = 1.0;
};

#endif // SKYPOLARWIDGET_H