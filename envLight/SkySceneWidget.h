#ifndef SKYSCENEWIDGET_H
#define SKYSCENEWIDGET_H

#include <QOpenGLBuffer>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>
#include <QPoint>
#include <QVector3D>
#include <QVector4D>

#include <array>
#include <memory>
#include <vector>

class QColor;
class QMatrix4x4;
class QMouseEvent;
class QOpenGLShaderProgram;
class QPainter;
class QString;
class QWheelEvent;

// ================================================================
// 天空三维几何状态
// 说明：该结构只描述 Viewer 几何，不参与 CIE 亮度计算。
// World ENU、CIE Sky ENU、太阳方向、有限天空球和 Viewer Camera 都使用世界坐标表达，
// 因而可以被 StandardSkyViewer、SkyPerspectiveWidget 或其它天空视图复用。
// ================================================================
struct SkySceneState
{
    double sphereRadius = 1.0;
    bool finiteSkySphere = true;
    QVector3D skyEastDirection{1.0f, 0.0f, 0.0f};
    QVector3D skyNorthDirection{0.0f, 1.0f, 0.0f};
    QVector3D skyUpDirection{0.0f, 0.0f, 1.0f};
    QVector3D sunDirectionWorld{0.0f, 1.0f, 1.0f};
    QVector3D cameraOriginWorld{0.0f, 0.0f, 0.0f};
    QVector3D cameraXAxisWorld{1.0f, 0.0f, 0.0f};
    QVector3D cameraYAxisWorld{0.0f, 0.0f, 1.0f};
    QVector3D cameraForwardWorld{0.0f, 1.0f, 0.0f};
    std::array<QVector3D, 4> frustumDirectionsWorld{QVector3D(-0.5f, 1.0f, 0.5f), QVector3D(0.5f, 1.0f, 0.5f), QVector3D(0.5f, 1.0f, -0.5f), QVector3D(-0.5f, 1.0f, -0.5f)};
};

// ================================================================
// CIE 天空三维几何查看器
// 说明：该控件类似 OpenGLSceneWidget，但不承担全景纹理和 CIE 数值渲染，只画几何关系：
// 1) 固定 World ENU；2) 可旋转的 CIE Sky ENU；3) 有限天空球与 CIE 地平线；
// 4) 太阳球面位置；5) Viewer Camera 位置、局部轴和视锥。
// ================================================================
class SkySceneWidget final : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core
{
    Q_OBJECT

public:
    // 功能：创建三维天空几何查看器。
    explicit SkySceneWidget(QWidget* parent = nullptr);

    // 功能：释放 OpenGL 资源。
    ~SkySceneWidget() override;

    // 功能：设置当前天空和相机几何状态并刷新视图。
    void setSceneState(const SkySceneState& state);

    // 功能：返回当前已规范化的天空和相机几何状态。
    const SkySceneState& sceneState() const;

protected:
    // 功能：初始化颜色着色器和动态线段缓冲区。
    void initializeGL() override;

    // 功能：同步 OpenGL viewport。
    void resizeGL(int width, int height) override;

    // 功能：绘制天空球、双 ENU、太阳、相机和视锥。
    void paintGL() override;

    // 功能：记录三维观察相机的轨迹球拖拽起点。
    void mousePressEvent(QMouseEvent* event) override;

    // 功能：左键拖动旋转三维观察相机，不修改 Viewer Camera 参数。
    void mouseMoveEvent(QMouseEvent* event) override;

    // 功能：滚轮调整三维场景观察距离，不修改天空参数。
    void wheelEvent(QWheelEvent* event) override;

private:
    // 功能：创建仅用于线段和点的 OpenGL 颜色着色器。
    void createShader();

    // 功能：绘制有限天空球经纬线。
    void drawSkySphere(const QMatrix4x4& mvp);

    // 功能：绘制固定 World ENU 坐标轴。
    void drawWorldAxes(const QMatrix4x4& mvp);

    // 功能：绘制用户设置后的 CIE Sky ENU 坐标轴和 CIE 地平线。
    void drawSkyAxesAndHorizon(const QMatrix4x4& mvp);

    // 功能：绘制太阳方向线和太阳球面位置。
    void drawSun(const QMatrix4x4& mvp);

    // 功能：绘制 Viewer Camera 位置、Xc/Yc/Zc 和四条视锥边线。
    void drawCamera(const QMatrix4x4& mvp);

    // 功能：使用动态 VBO 绘制线段或折线。
    void drawLines(const std::vector<QVector3D>& vertices, const QVector4D& color, const QMatrix4x4& mvp, GLenum primitive, float lineWidth);

    // 功能：使用动态 VBO 绘制场景标记点。
    void drawPoints(const std::vector<QVector3D>& vertices, const QVector4D& color, const QMatrix4x4& mvp, float pointSize);

    // 功能：计算 Viewer Camera 射线与有限天空球的正向交点。
    bool raySphereIntersectionPoint(const QVector3D& origin, const QVector3D& direction, QVector3D& hitPoint) const;

    // 功能：把三维点投影到控件像素坐标，用于绘制文字标签。
    QPointF projectToWidget(const QVector3D& point, const QMatrix4x4& mvp) const;

    // 功能：绘制 ENU、太阳和 Camera 的二维文字标签。
    void paintLabels(const QMatrix4x4& mvp);

    // 功能：在投影位置绘制一个标签，避免在 paintLabels() 内使用 lambda。
    void drawProjectedLabel(QPainter& painter, const QVector3D& point, const QString& text, const QColor& color, const QMatrix4x4& mvp) const;

    // 功能：规范化方向向量；输入退化时使用 fallback。
    static QVector3D normalizedOr(const QVector3D& value, const QVector3D& fallback);

private:
    SkySceneState m_state;
    std::unique_ptr<QOpenGLShaderProgram> m_colorProgram;
    QOpenGLVertexArrayObject m_lineVao;
    QOpenGLBuffer m_lineVbo;
    bool m_glReady = false;
    float m_viewYaw = 35.0f;
    float m_viewPitch = 24.0f;
    float m_viewDistance = 3.25f;
    QPoint m_lastMousePosition;
};

#endif // SKYSCENEWIDGET_H
