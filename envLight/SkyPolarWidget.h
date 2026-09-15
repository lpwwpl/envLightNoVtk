#ifndef SKYPOLARWIDGET_H
#define SKYPOLARWIDGET_H

#include <QDate>
#include <QVector>
#include <QVector3D>
#include <QWidget>

#include "SunSky.hpp"

class QColor;
class QPaintEvent;
class QPainter;
class QPainterPath;
class QPointF;
class QRectF;

// ================================================================
// 天空极坐标离散单元
// 每个单元保存自身立体角 dΩ、CIE 相对亮度 Lrel，以及经过 DHI 标定后对
// 水平散射辐照度的贡献。这样极坐标图既能展示方向分布，也能核对半球积分。
// ================================================================
struct SkyCell {
    double altitudeMin = 0.0;
    double altitudeMax = 0.0;
    double azimuthMin = 0.0;
    double azimuthMax = 0.0;
    double solidAngle = 0.0;
    double relativeValue = 0.0;
    double displayValue = 0.0;
};

// ================================================================
// CIE Sky 半球极坐标图
// 处理流程：半球近等立体角离散 -> CIE 相对天空 -> DHI 绝对标定 ->
// 每个单元的水平面贡献 L*cos(theta)*dΩ -> 热力图显示。
// ================================================================
class SkyPolarWidget final : public QWidget
{
    Q_OBJECT

public:
    // 功能：创建天空极坐标图并建立默认半球离散网格。
    explicit SkyPolarWidget(QWidget* parent = nullptr);

    // 功能：设置高度方向的目标分割角度并重新构建近似等立体角网格。
    void setSubdivisionAngle(double angleDegrees);

    // 功能：设置 0~14 的 CIE 标准天空类型。
    void setCieSkyType(int type);

    // 功能：切换为自定义 CIE 系数并重新计算天空分布。
    void setCustomCoefficients(const SSLib::CIESkyCoefficients& coefficients);

    // 功能：设置水平散射辐照度 DHI，单位 W/m²。
    void setDiffuseHorizontalIrradiance(double dhi);

    // 功能：直接设置 ENU 中的太阳单位方向。
    void setSunDirection(const QVector3D& direction);

    // 功能：设置太阳轨迹计算使用的纬度、经度和时区。
    void setLocation(double latitude, double longitude, double timeZone);

    // 功能：设置日期与当地小数小时，并据此刷新太阳方向。
    void setDateTime(const QDate& date, double decimalHour);

    // 功能：控制天空离散单元边界是否显示。
    void setShowSegmentOutlines(bool enabled);

    // 功能：控制全年太阳轨迹辅助线是否显示。
    void setShowSunPaths(bool enabled);

    // 功能：返回当前天空离散单元数量。
    int segmentCount() const;

    // 功能：返回当前高度带数量。
    int bandCount() const;

protected:
    // 功能：绘制天空单元、网格、方位刻度、太阳和色标。
    void paintEvent(QPaintEvent* event) override;

    // 功能：返回控件建议的最小尺寸。
    QSize minimumSizeHint() const override;

private:
    // 功能：按照目标角度重建半球离散单元。
    void rebuildSubdivision();

    // 功能：计算各单元相对亮度，并按 DHI 完成绝对标定。
    void rebuildSkyValues();

    // 功能：计算单元中心对应的 ENU 单位方向。
    QVector3D cellCenterDirection(const SkyCell& cell) const;

    // 功能：把上半球方向投影为极坐标图中的二维点。
    QPointF projectDirection(const QVector3D& direction, const QPointF& center, double radius) const;

    // 功能：创建一个天空离散单元的二维扇环路径。
    QPainterPath createCellPath(const SkyCell& cell, const QPointF& center, double radius) const;

    // 功能：把 0~1 归一化数值映射成热力图颜色。
    QColor heatMapColor(double normalized) const;

    // 功能：绘制按绝对辐照度贡献着色的天空离散单元。
    void drawSkyCells(QPainter& painter, const QRectF& skyRect);

    // 功能：绘制高度圈和南北/东西参考线。
    void drawGrid(QPainter& painter, const QRectF& skyRect);

    // 功能：绘制 0~360° 方位刻度及 N/E/S/W 标识。
    void drawAzimuthTicks(QPainter& painter, const QRectF& skyRect);

    // 功能：绘制当前太阳位置。
    void drawSun(QPainter& painter, const QRectF& skyRect);

    // 功能：绘制每月典型日轨迹与整年小时线。
    void drawSunPaths(QPainter& painter, const QRectF& skyRect);

    // 功能：绘制天空单元辐照度贡献色标。
    void drawColorBar(QPainter& painter, const QRectF& skyRect);

private:
    QVector<SkyCell> m_cells;

    double m_subdivisionAngle = 5.0;
    double m_dhi = 100.0;

    int m_cieSkyType = 11; // 0-based，对应 CIE Type 12
    bool m_customMode = false;
    SSLib::CIESkyCoefficients m_customCoeffs;

    QVector3D m_sunDirection{0.5f, -0.5f, 0.7071f};

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
