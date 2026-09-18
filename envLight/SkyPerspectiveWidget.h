#ifndef SKYPERSPECTIVEWIDGET_H
#define SKYPERSPECTIVEWIDGET_H

#include <QColor>
#include <QImage>
#include <QPoint>
#include <QVector3D>
#include <QWidget>
#include <memory>

#include "SunSky.hpp"
#include "WeatherEffects.h"

class QMouseEvent;
class QContextMenuEvent;
class QPaintEvent;
class QEvent;
class QPainter;
class QResizeEvent;
class QRect;
class QRectF;
class QString;
class QTimer;
class QWheelEvent;
class EnvironmentLight;
class PerspectiveColorBarWidget;
struct SkySceneState;

// ================================================================
// CIE Sky 绝对标定方式
// CIE Standard General Sky 首先给出的是“相对亮度分布”，即不同天空方向之间的
// 明暗比例；要得到 cd/m²、lx 或 W/m² 等绝对量，还必须用 EPW/用户输入进行标定。
// ================================================================
enum class SkyAbsoluteScaleMode {
    ZenithLuminance,              // 天顶亮度，cd/m²
    DiffuseHorizontalIlluminance, // 水平散射照度，lx
    DiffuseHorizontalIrradiance   // 水平散射辐照度，W/m²
};

// ================================================================
// 预览颜色模式
// ================================================================
enum class SkyColorMode {
    GrayscaleLuminance,
    FalseColor,
    NaturalPreview,
    // Speos-style scalar palettes used by both on-screen Display and PNG Export.
    BlackToWhiteColor,
    WhiteToBlackColor,
    BlueToRed,
    RedToBlue
};

// ================================================================
// Tone Mapping 参考值模式
// ================================================================
enum class SkyToneMapMode {
    FixedReference,
    AutoPeak
};

// ================================================================
// Radiance Sensor 结果类型
// ================================================================
enum class SkyMeasurementType {
    Photometric = 0, // cd/m²
    Radiometric = 1, // W/(m²·sr)，对指定光谱范围积分
    Colorimetric = 2,// XYZ/sRGB；标量显示使用 Y 分量
    Spectral = 3     // W/(m²·sr·nm)，显示指定波长
};

// ================================================================
// Radiance Sensor 结果层
// ================================================================
enum class SkyMeasurementLayer {
    Combined = 0,
    DiffuseSkyOnly = 1,
    DirectSunOnly = 2
};

// ================================================================
// CIE 标准色度观察者
// ================================================================
enum class SkyObserverType {
    CIE1931_2Deg = 0,
    CIE1964_10Deg = 1
};

// ================================================================
// CIE Sky 透视渲染参数
// 数值链路保持“相对天空分布 -> 绝对标定 -> 太阳盘 -> 传感器/显示”四层分离：
// 1) cieSkyType/coefficients 决定 CIE 天空形状；
// 2) scaleMode/targetValue 决定天空绝对尺度；
// 3) directNormalValue 独立决定太阳盘，不参与漫射天空二次缩放；
// 4) color/tone mapping 只影响预览，不修改物理量。
// ================================================================
struct SkyPerspectiveParameters {
    int cieSkyType = 11; // 0-based，对应 CIE Type 12
    bool customCoefficients = false;
    SSLib::CIESkyCoefficients coefficients;

    SkyAbsoluteScaleMode scaleMode = SkyAbsoluteScaleMode::DiffuseHorizontalIrradiance;
    double targetValue = 100.0;

    // 太阳盘与漫射天空独立标定：辐照度模式下为 DNI(W/m²)，光度模式下为
    // Direct Normal Illuminance(lx)。渲染时再除以太阳圆盘立体角得到方向亮度。
    double directNormalValue = 600.0;

    QVector3D sunDirection{0.5f, -0.5f, 0.7071f};

    // CIE 天空局部 ENU 基在世界坐标中的方向。Viewer Camera 可独立工作在世界 ENU，最终采样方向再通过 worldToSky() 投影到此天空基。
    QVector3D skyEastDirection{1.0f, 0.0f, 0.0f};
    QVector3D skyNorthDirection{0.0f, 1.0f, 0.0f};
    QVector3D skyZenithDirection{0.0f, 0.0f, 1.0f};
    bool cameraRelativeToSkyBasis = true;
    std::shared_ptr<const EnvironmentLight> environmentLight;
    QColor baseGroundColor{35, 37, 40};

    // 导航相机：Azimuth 从 North 顺时针；Pitch 相对地平面向上为正。
    double cameraAzimuthDeg = 180.0;
    double cameraPitchDeg = 20.0;
    double verticalFovDeg = 90.0;
    double cameraRollDeg = 0.0;
    double horizontalFovDeg = 90.0;
    bool localCamera = false;
    QVector3D cameraPositionLocal{0.0f, 0.0f, 0.0f};

    // StandardSkyViewer 可启用有限半径天空球。相机位移先参与球面求交，再用“球心 -> 交点”方向查询 CIE 天空，因此 Xc/Yc/Zc 会产生可视化视差。
    bool useFiniteSkySphere = false;
    double skySphereRadius = 1.0;

    // Frame/Focal Radiance Sensor 投影参数。
    bool useSensorFrameProjection = false;
    QVector3D sensorOriginWorldMm{0.0f, 0.0f, 0.0f};
    QVector3D sensorXAxisWorld{1.0f, 0.0f, 0.0f};
    QVector3D sensorYAxisWorld{0.0f, 1.0f, 0.0f};
    QVector3D sensorForwardWorld{0.0f, 0.0f, 1.0f};
    double sensorFocalMm = 50.0;
    double sensorXStartMm = -50.0;
    double sensorXEndMm = 50.0;
    double sensorYStartMm = -50.0;
    double sensorYEndMm = 50.0;
    bool sensorXMirror = false;
    bool sensorYMirror = false;

    // 兼容 Speos Direct Integration Angle。当前解析模型仅保存，不做蒙特卡洛模糊。
    double directIntegrationAngleDeg = 5.0;

    SkyMeasurementType measurementType = SkyMeasurementType::Photometric;
    SkyMeasurementLayer measurementLayer = SkyMeasurementLayer::Combined;
    SkyObserverType observerType = SkyObserverType::CIE1931_2Deg;

    // CIE Standard General Sky 只定义亮度分布，不定义 SPD；这里使用归一化 Planck 形状做转换。
    double spectralTemperatureK = 6500.0;
    double spectralStartNm = 400.0;
    double spectralEndNm = 700.0;
    int spectralSampling = 13;
    bool spectralDisplayAllWavelengths = true;
    double displayWavelengthNm = 550.0;

    SkyColorMode colorMode = SkyColorMode::NaturalPreview;
    SkyToneMapMode toneMapMode = SkyToneMapMode::FixedReference;
    double displayReferenceValue = 50.0;
    double exposure = 1.0;
    double gamma = 2.2;

    bool showHorizon = true;
    bool showSunDisk = true;
    bool showSunGlow = true;
    double sunAngularRadiusDeg = 0.2665;

    // 天气仅作为显示层，不修改 CIE Sky 的数学分布和绝对标定结果。
    WeatherVisualState weather;
    bool animateWeather = true;
    bool showWeatherParticles = true;
    bool showWeatherGround = true;
};

// ================================================================
// CIE Sky 透视视图控件
// ================================================================
class SkyPerspectiveWidget final : public QWidget
{
    Q_OBJECT

public:
    // 功能：创建透视天空控件并初始化天气动画定时器。
    explicit SkyPerspectiveWidget(QWidget* parent = nullptr);

    // 功能：设置渲染参数，完成边界检查、坐标基正交化并刷新预览。
    void setParameters(const SkyPerspectiveParameters& parameters);

    // 功能：返回当前已经规范化后的渲染参数。
    const SkyPerspectiveParameters& parameters() const;

    // 功能：导出与当前渲染完全一致的三维几何状态，供独立 SkySceneWidget 可视化太阳、双 ENU、有限天空球和 Viewer Camera。
    SkySceneState sceneState() const;

    // 功能：按指定分辨率离屏渲染天空图像。
    QImage renderToImage(const QSize& imageSize) const;

    // 功能：按指定分辨率渲染并保存 PNG 文件。
    bool savePng(const QString& filePath, const QSize& imageSize) const;

signals:
    // 功能：导航相机交互变化后通知外部同步 UI 参数。
    void cameraChanged(double azimuthDeg, double pitchDeg, double rollDeg, double horizontalFovDeg, double verticalFovDeg);

protected:
    // 功能：绘制缓存天空、天气覆盖层、地平线与状态文字。
    void paintEvent(QPaintEvent* event) override;

    // 功能：窗口尺寸变化时重建交互预览缓存。
    void resizeEvent(QResizeEvent* event) override;

    // 功能：记录导航相机拖拽起点。
    void mousePressEvent(QMouseEvent* event) override;

    // 功能：右击弹出显示/隐藏 Colorbar 菜单。
    void contextMenuEvent(QContextMenuEvent* event) override;

    // 功能：左键拖拽时调整相机方位角与仰角，或拖动 Colorbar/执行吸管取值。
    void mouseMoveEvent(QMouseEvent* event) override;

    // 功能：结束 Colorbar 拖动。
    void mouseReleaseEvent(QMouseEvent* event) override;

    // 功能：鼠标离开时清除悬浮读数。
    void leaveEvent(QEvent* event) override;

    // 功能：鼠标滚轮调整垂直视场角。
    void wheelEvent(QWheelEvent* event) override;

private slots:
    // 功能：推进雨雪粒子动画时间并触发重绘。
    void advanceWeatherAnimation();

private:
    // 功能：重建用于窗口显示的低成本预览图。
    void rebuildPreview();

    // 功能：渲染不含粒子覆盖层的天空/地面基础图像。
    QImage renderBaseImage(const QSize& imageSize) const;

    // 功能：在已经生成的图像上绘制雨、雪或冰雹粒子。
    void drawWeatherOverlay(QPainter& painter, const QRectF& targetRect, double animationSeconds) const;

    // 功能：计算给定世界方向上的 CIE Sky 相对亮度。
    double relativeSkyValue(const QVector3D& direction) const;

    // 功能：根据绝对标定方式计算 CIE 相对分布到真实量纲的比例系数。
    double absoluteScale() const;

    // 功能：把世界方向投影到天空局部 ENU 基。
    QVector3D worldToSky(const QVector3D& worldDirection) const;

    // 功能：把天空局部 ENU 方向转换为世界方向。
    QVector3D skyToWorld(const QVector3D& skyDirection) const;

    // 功能：把物理标量压缩到 0~1 的显示亮度。
    double toneMappedValue(double value, double referenceValue) const;

    // 功能：根据 CIE 类型估计自然预览的晴朗程度。
    double clearSkyFactor() const;

    // 功能：根据降水和雾估计直射太阳的显示衰减。
    double atmosphericAttenuation() const;

    // 功能：为输出像素构造世界坐标中的单位相机射线。
    QVector3D cameraRay(int x, int y, int width, int height) const;

    // 功能：把传统 Local Camera 向量按与主界面一致的 Rz(Yaw)*Ry(Pitch)*Rx(Roll) 语义转换到世界 ENU；该变换只属于 Viewer Camera，不受 CIE 天空 ENU 基影响。
    QVector3D localCameraVectorToWorldENU(const QVector3D& cameraVector) const;

    // 功能：把 Viewer Camera 的 Xc/Yc/Zc 转换为世界 ENU 中的有限天空球相机原点，明确与 CIE 天空基变换分离。
    QVector3D cameraOriginWorld() const;

    // 功能：有限天空球模式下计算球面交点方向；关闭该模式时直接返回无穷远天空的相机射线方向。
    bool sampleDirectionForPixel(int x, int y, int width, int height, const QVector3D& sphereOriginWorld, QVector3D& direction) const;

    // 功能：计算射线与以世界原点为球心的有限天空球交点，并返回“球心 -> 交点”的单位方向。
    static bool raySphereIntersection(const QVector3D& origin, const QVector3D& rayDirection, double radius, QVector3D& sphereDirection);

    // 功能：只旋转天空局部 ENU 向量到世界坐标，不改变向量长度；用于相机位置。
    QVector3D skyVectorToWorld(const QVector3D& skyVector) const;

    // 功能：用 CIE 标量亮度生成不参与数值计算的自然天空预览颜色。
    QColor naturalPreviewColor(const QVector3D& direction, double normalizedBrightness, double sunCosine, double directStrength) const;

    // 功能：把雾、云量和降水造成的大气泛白应用到预览颜色。
    QColor applyWeatherAtmosphere(const QColor& color) const;

    // 功能：根据积雪或降雨状态返回地面显示颜色。
    QColor groundColor() const;

    // 功能：判断当前天气是否需要启动粒子动画。
    bool weatherNeedsAnimation() const;

    // 功能：绘制吸管十字线和值；Colorbar 本体由独立浮动窗口绘制。
    void drawEyedropperOverlay(QPainter& painter);

    // 功能：创建/显示或隐藏独立于 PerspectiveWidget 的浮动 Colorbar。
    void showColorBarWindow();
    void hideColorBarWindow();

    // 功能：返回当前颜色模式对应的色标颜色。
    QColor colorBarColor(double normalized) const;

    // 功能：返回 Colorbar 标题/单位。
    QString colorBarTitle() const;

    // 功能：把 Colorbar 归一化位置反解为真实物理值，并格式化显示。
    double colorBarValueFromNormalized(double normalized) const;
    QString formatMeasurementValue(double value) const;

    // 功能：在当前 widget 像素处重新计算传感器标量，供吸管读取。
    bool measurementValueAtWidgetPoint(const QPoint& point, double& value) const;

    // 功能：把 0~1 标量映射为伪彩色。
    static QColor falseColor(double normalized);
    static QColor blackToWhiteColor(double normalized);
    static QColor whiteToBlackColor(double normalized);
    static QColor blueToRedColor(double normalized);
    static QColor redToBlueColor(double normalized);

    // 功能：把数值限制在指定闭区间。
    static double clamp(double value, double low, double high);

    // 功能：生成稳定的 0~1 伪随机数，用于天气粒子位置。
    static double hash01(int index, int salt);

    // 功能：在两个三维颜色向量之间做线性插值。
    static QVector3D mix(const QVector3D& a, const QVector3D& b, double t);

private:
    friend class PerspectiveColorBarWidget;

    SkyPerspectiveParameters m_parameters;
    QImage m_preview;
    QPoint m_lastMousePosition;
    QTimer* m_weatherTimer = nullptr;
    double m_animationSeconds = 0.0;

    // Display-only floating colorbar state. Colorbar 是独立顶层 Tool 窗口，可浮动在 PerspectiveWidget 外。
    bool m_colorBarVisible = false;
    mutable double m_lastColorBarMaximum = 1.0;
    bool m_colorBarLogScale = false;
    PerspectiveColorBarWidget* m_colorBarWidget = nullptr;
    bool m_eyedropperEnabled = false;
    bool m_eyedropperValid = false;
    QPoint m_eyedropperPos;
    double m_eyedropperValue = 0.0;
};

#endif // SKYPERSPECTIVEWIDGET_H
