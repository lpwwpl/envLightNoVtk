#include "SkySceneWidget.h"

#include <QColor>
#include <QMatrix4x4>
#include <QMouseEvent>
#include <QOpenGLShaderProgram>
#include <QPainter>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace
{
constexpr double kPi = 3.14159265358979323846;
constexpr double kDegToRad = kPi / 180.0;
}

SkySceneWidget::SkySceneWidget(QWidget* parent) : QOpenGLWidget(parent), m_lineVbo(QOpenGLBuffer::VertexBuffer)
{
    setMinimumSize(420, 220);
    setFocusPolicy(Qt::StrongFocus);
}

SkySceneWidget::~SkySceneWidget()
{
    if (!context())
    {
        return;
    }
    makeCurrent();
    m_lineVao.destroy();
    m_lineVbo.destroy();
    m_colorProgram.reset();
    doneCurrent();
}

void SkySceneWidget::setSceneState(const SkySceneState& state)
{
    m_state = state;
    m_state.sphereRadius = std::isfinite(m_state.sphereRadius) && m_state.sphereRadius > 1.0e-6 ? m_state.sphereRadius : 1.0;
    m_state.skyEastDirection = normalizedOr(m_state.skyEastDirection, QVector3D(1.0f, 0.0f, 0.0f));
    m_state.skyNorthDirection = normalizedOr(m_state.skyNorthDirection, QVector3D(0.0f, 1.0f, 0.0f));
    m_state.skyUpDirection = normalizedOr(m_state.skyUpDirection, QVector3D(0.0f, 0.0f, 1.0f));
    m_state.sunDirectionWorld = normalizedOr(m_state.sunDirectionWorld, m_state.skyUpDirection);
    m_state.cameraXAxisWorld = normalizedOr(m_state.cameraXAxisWorld, QVector3D(1.0f, 0.0f, 0.0f));
    m_state.cameraYAxisWorld = normalizedOr(m_state.cameraYAxisWorld, QVector3D(0.0f, 0.0f, 1.0f));
    m_state.cameraForwardWorld = normalizedOr(m_state.cameraForwardWorld, QVector3D(0.0f, 1.0f, 0.0f));
    for (QVector3D& direction : m_state.frustumDirectionsWorld)
    {
        direction = normalizedOr(direction, m_state.cameraForwardWorld);
    }
    update();
}

const SkySceneState& SkySceneWidget::sceneState() const
{
    return m_state;
}

void SkySceneWidget::initializeGL()
{
    initializeOpenGLFunctions();
    glClearColor(0.055f, 0.065f, 0.085f, 1.0f);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    createShader();
    m_lineVao.create();
    m_lineVbo.create();
    m_glReady = true;
}

void SkySceneWidget::resizeGL(int width, int height)
{
    glViewport(0, 0, width, height);
}

void SkySceneWidget::paintGL()
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (!m_glReady || !m_colorProgram)
    {
        return;
    }

    const float yaw = static_cast<float>(m_viewYaw * kDegToRad);
    const float pitch = static_cast<float>(m_viewPitch * kDegToRad);
    const float cosPitch = std::cos(pitch);
    const QVector3D eye(m_viewDistance * cosPitch * std::cos(yaw), m_viewDistance * cosPitch * std::sin(yaw), m_viewDistance * std::sin(pitch));

    QMatrix4x4 view;
    view.lookAt(eye, QVector3D(0.0f, 0.0f, 0.0f), QVector3D(0.0f, 0.0f, 1.0f));
    QMatrix4x4 projection;
    const float aspect = height() > 0 ? static_cast<float>(width()) / static_cast<float>(height()) : 1.0f;
    projection.perspective(42.0f, aspect, 0.03f, 30.0f);
    const QMatrix4x4 mvp = projection * view;

    drawSkySphere(mvp);
    drawWorldAxes(mvp);
    drawSkyAxesAndHorizon(mvp);
    drawSun(mvp);
    drawCamera(mvp);
    paintLabels(mvp);
}

void SkySceneWidget::mousePressEvent(QMouseEvent* event)
{
    m_lastMousePosition = event->pos();
    QOpenGLWidget::mousePressEvent(event);
}

void SkySceneWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (event->buttons() & Qt::LeftButton)
    {
        const QPoint delta = event->pos() - m_lastMousePosition;
        m_lastMousePosition = event->pos();
        m_viewYaw += delta.x() * 0.45f;
        m_viewPitch = std::clamp(m_viewPitch + delta.y() * 0.35f, -85.0f, 85.0f);
        update();
    }
    QOpenGLWidget::mouseMoveEvent(event);
}

void SkySceneWidget::wheelEvent(QWheelEvent* event)
{
    const float steps = event->angleDelta().y() / 120.0f;
    m_viewDistance = std::clamp(m_viewDistance * std::pow(0.88f, steps), 1.55f, 9.0f);
    update();
    event->accept();
}

void SkySceneWidget::createShader()
{
    m_colorProgram = std::make_unique<QOpenGLShaderProgram>();
    m_colorProgram->addShaderFromSourceCode(QOpenGLShader::Vertex, "#version 330 core\nlayout(location = 0) in vec3 aPos;\nuniform mat4 uMVP;\nvoid main(){ gl_Position = uMVP * vec4(aPos, 1.0); }");
    m_colorProgram->addShaderFromSourceCode(QOpenGLShader::Fragment, "#version 330 core\nuniform vec4 uColor;\nout vec4 FragColor;\nvoid main(){ FragColor = uColor; }");
    m_colorProgram->link();
}

void SkySceneWidget::drawSkySphere(const QMatrix4x4& mvp)
{
    const double radius = m_state.sphereRadius;
    constexpr int segments = 96;
    std::vector<QVector3D> lines;

    for (int altitudeDeg = -75; altitudeDeg <= 75; altitudeDeg += 15)
    {
        const double altitude = altitudeDeg * kDegToRad;
        const double horizontal = radius * std::cos(altitude);
        const double up = radius * std::sin(altitude);
        for (int index = 0; index < segments; ++index)
        {
            const double a0 = 2.0 * kPi * index / segments;
            const double a1 = 2.0 * kPi * (index + 1) / segments;
            lines.push_back(QVector3D(static_cast<float>(horizontal * std::sin(a0)), static_cast<float>(horizontal * std::cos(a0)), static_cast<float>(up)));
            lines.push_back(QVector3D(static_cast<float>(horizontal * std::sin(a1)), static_cast<float>(horizontal * std::cos(a1)), static_cast<float>(up)));
        }
    }

    for (int azimuthDeg = 0; azimuthDeg < 360; azimuthDeg += 30)
    {
        const double azimuth = azimuthDeg * kDegToRad;
        for (int index = 0; index < segments / 2; ++index)
        {
            const double elevation0 = -0.5 * kPi + kPi * index / (segments / 2);
            const double elevation1 = -0.5 * kPi + kPi * (index + 1) / (segments / 2);
            lines.push_back(QVector3D(static_cast<float>(radius * std::cos(elevation0) * std::sin(azimuth)), static_cast<float>(radius * std::cos(elevation0) * std::cos(azimuth)), static_cast<float>(radius * std::sin(elevation0))));
            lines.push_back(QVector3D(static_cast<float>(radius * std::cos(elevation1) * std::sin(azimuth)), static_cast<float>(radius * std::cos(elevation1) * std::cos(azimuth)), static_cast<float>(radius * std::sin(elevation1))));
        }
    }

    drawLines(lines, QVector4D(0.72f, 0.77f, 0.84f, 0.24f), mvp, GL_LINES, 1.0f);
}

void SkySceneWidget::drawWorldAxes(const QMatrix4x4& mvp)
{
    const float length = static_cast<float>(m_state.sphereRadius * 1.28);
    drawLines({QVector3D(0.0f, 0.0f, 0.0f), QVector3D(length, 0.0f, 0.0f)}, QVector4D(1.0f, 0.24f, 0.20f, 1.0f), mvp, GL_LINES, 2.2f);
    drawLines({QVector3D(0.0f, 0.0f, 0.0f), QVector3D(0.0f, length, 0.0f)}, QVector4D(0.20f, 0.95f, 0.35f, 1.0f), mvp, GL_LINES, 2.2f);
    drawLines({QVector3D(0.0f, 0.0f, 0.0f), QVector3D(0.0f, 0.0f, length)}, QVector4D(0.25f, 0.55f, 1.0f, 1.0f), mvp, GL_LINES, 2.2f);
}

void SkySceneWidget::drawSkyAxesAndHorizon(const QMatrix4x4& mvp)
{
    const float axisLength = static_cast<float>(m_state.sphereRadius * 1.14);
    drawLines({QVector3D(), m_state.skyEastDirection * axisLength}, QVector4D(1.0f, 0.58f, 0.20f, 0.95f), mvp, GL_LINES, 2.0f);
    drawLines({QVector3D(), m_state.skyNorthDirection * axisLength}, QVector4D(0.20f, 0.94f, 0.78f, 0.95f), mvp, GL_LINES, 2.0f);
    drawLines({QVector3D(), m_state.skyUpDirection * axisLength}, QVector4D(0.76f, 0.48f, 1.0f, 0.95f), mvp, GL_LINES, 2.0f);

    constexpr int segments = 128;
    std::vector<QVector3D> horizon;
    horizon.reserve(segments + 1);
    for (int index = 0; index <= segments; ++index)
    {
        const double angle = 2.0 * kPi * index / segments;
        const QVector3D point = static_cast<float>(m_state.sphereRadius * std::sin(angle)) * m_state.skyEastDirection + static_cast<float>(m_state.sphereRadius * std::cos(angle)) * m_state.skyNorthDirection;
        horizon.push_back(point * 1.002f);
    }
    drawLines(horizon, QVector4D(0.96f, 0.88f, 0.34f, 0.92f), mvp, GL_LINE_STRIP, 2.2f);
}

void SkySceneWidget::drawSun(const QMatrix4x4& mvp)
{
    const QVector3D sunPosition = m_state.sunDirectionWorld * static_cast<float>(m_state.sphereRadius * 1.015);
    const double sunAboveHorizon = QVector3D::dotProduct(m_state.sunDirectionWorld, m_state.skyUpDirection);
    const QVector4D sunColor = sunAboveHorizon >= 0.0 ? QVector4D(1.0f, 0.84f, 0.20f, 1.0f) : QVector4D(0.65f, 0.48f, 0.22f, 0.65f);
    drawLines({QVector3D(), sunPosition}, QVector4D(sunColor.x(), sunColor.y(), sunColor.z(), 0.72f), mvp, GL_LINES, 1.8f);
    drawPoints({sunPosition}, sunColor, mvp, 11.0f);
}

void SkySceneWidget::drawCamera(const QMatrix4x4& mvp)
{
    const QVector3D origin = m_state.cameraOriginWorld;
    const float axisLength = static_cast<float>(m_state.sphereRadius * 0.22);
    drawPoints({origin}, QVector4D(1.0f, 0.30f, 0.28f, 1.0f), mvp, 10.0f);
    drawLines({origin, origin + axisLength * m_state.cameraXAxisWorld}, QVector4D(1.0f, 0.30f, 0.24f, 1.0f), mvp, GL_LINES, 2.4f);
    drawLines({origin, origin + axisLength * m_state.cameraYAxisWorld}, QVector4D(0.35f, 0.90f, 1.0f, 1.0f), mvp, GL_LINES, 2.4f);
    drawLines({origin, origin + axisLength * m_state.cameraForwardWorld}, QVector4D(0.35f, 1.0f, 0.42f, 1.0f), mvp, GL_LINES, 3.0f);

    std::array<QVector3D, 4> hits;
    std::array<bool, 4> hitValid{false, false, false, false};
    std::vector<QVector3D> rays;
    for (int index = 0; index < 4; ++index)
    {
        QVector3D hit;
        hitValid[index] = raySphereIntersectionPoint(origin, m_state.frustumDirectionsWorld[index], hit);
        if (hitValid[index])
        {
            hits[index] = hit;
            rays.push_back(origin);
            rays.push_back(hit);
        }
    }
    drawLines(rays, QVector4D(0.28f, 1.0f, 0.45f, 0.78f), mvp, GL_LINES, 1.5f);

    if (hitValid[0] && hitValid[1] && hitValid[2] && hitValid[3])
    {
        std::vector<QVector3D> roi{hits[0] * 1.003f, hits[1] * 1.003f, hits[2] * 1.003f, hits[3] * 1.003f, hits[0] * 1.003f};
        drawLines(roi, QVector4D(0.95f, 0.78f, 0.18f, 1.0f), mvp, GL_LINE_STRIP, 2.2f);
    }
}

void SkySceneWidget::drawLines(const std::vector<QVector3D>& vertices, const QVector4D& color, const QMatrix4x4& mvp, GLenum primitive, float lineWidth)
{
    if (!m_glReady || !m_colorProgram || vertices.empty())
    {
        return;
    }
    m_lineVao.bind();
    m_lineVbo.bind();
    m_lineVbo.setUsagePattern(QOpenGLBuffer::DynamicDraw);
    m_lineVbo.allocate(vertices.data(), static_cast<int>(vertices.size() * sizeof(QVector3D)));
    m_colorProgram->bind();
    m_colorProgram->setUniformValue("uMVP", mvp);
    m_colorProgram->setUniformValue("uColor", color);
    m_colorProgram->enableAttributeArray(0);
    m_colorProgram->setAttributeBuffer(0, GL_FLOAT, 0, 3, sizeof(QVector3D));
    glLineWidth(lineWidth);
    glDrawArrays(primitive, 0, static_cast<GLsizei>(vertices.size()));
    glLineWidth(1.0f);
    m_colorProgram->disableAttributeArray(0);
    m_colorProgram->release();
    m_lineVbo.release();
    m_lineVao.release();
}

void SkySceneWidget::drawPoints(const std::vector<QVector3D>& vertices, const QVector4D& color, const QMatrix4x4& mvp, float pointSize)
{
    if (!m_glReady || !m_colorProgram || vertices.empty())
    {
        return;
    }
    m_lineVao.bind();
    m_lineVbo.bind();
    m_lineVbo.setUsagePattern(QOpenGLBuffer::DynamicDraw);
    m_lineVbo.allocate(vertices.data(), static_cast<int>(vertices.size() * sizeof(QVector3D)));
    m_colorProgram->bind();
    m_colorProgram->setUniformValue("uMVP", mvp);
    m_colorProgram->setUniformValue("uColor", color);
    m_colorProgram->enableAttributeArray(0);
    m_colorProgram->setAttributeBuffer(0, GL_FLOAT, 0, 3, sizeof(QVector3D));
    glPointSize(pointSize);
    glDrawArrays(GL_POINTS, 0, static_cast<GLsizei>(vertices.size()));
    glPointSize(1.0f);
    m_colorProgram->disableAttributeArray(0);
    m_colorProgram->release();
    m_lineVbo.release();
    m_lineVao.release();
}

bool SkySceneWidget::raySphereIntersectionPoint(const QVector3D& origin, const QVector3D& direction, QVector3D& hitPoint) const
{
    if (!m_state.finiteSkySphere || direction.lengthSquared() < 1.0e-12f)
    {
        return false;
    }
    const QVector3D ray = direction.normalized();
    const double radius = m_state.sphereRadius;
    const double b = 2.0 * QVector3D::dotProduct(origin, ray);
    const double c = QVector3D::dotProduct(origin, origin) - radius * radius;
    const double discriminant = b * b - 4.0 * c;
    if (discriminant < 0.0)
    {
        return false;
    }
    const double root = std::sqrt(discriminant);
    const double t1 = (-b - root) * 0.5;
    const double t2 = (-b + root) * 0.5;
    const double t = t1 > 1.0e-6 ? t1 : (t2 > 1.0e-6 ? t2 : -1.0);
    if (t <= 1.0e-6)
    {
        return false;
    }
    hitPoint = origin + static_cast<float>(t) * ray;
    return true;
}

QPointF SkySceneWidget::projectToWidget(const QVector3D& point, const QMatrix4x4& mvp) const
{
    const QVector4D clip = mvp * QVector4D(point, 1.0f);
    if (std::abs(clip.w()) < 1.0e-6f)
    {
        return QPointF(-10000.0, -10000.0);
    }
    const QVector3D ndc = clip.toVector3DAffine();
    return QPointF((ndc.x() * 0.5 + 0.5) * width(), (1.0 - (ndc.y() * 0.5 + 0.5)) * height());
}

void SkySceneWidget::paintLabels(const QMatrix4x4& mvp)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const float worldLength = static_cast<float>(m_state.sphereRadius * 1.28);
    const float skyLength = static_cast<float>(m_state.sphereRadius * 1.14);
    const float cameraAxisLength = static_cast<float>(m_state.sphereRadius * 0.22);
    drawProjectedLabel(painter, QVector3D(worldLength, 0.0f, 0.0f), tr("World E"), QColor(255, 92, 72), mvp);
    drawProjectedLabel(painter, QVector3D(0.0f, worldLength, 0.0f), tr("World N"), QColor(80, 240, 100), mvp);
    drawProjectedLabel(painter, QVector3D(0.0f, 0.0f, worldLength), tr("World U"), QColor(80, 150, 255), mvp);
    drawProjectedLabel(painter, m_state.skyEastDirection * skyLength, tr("CIE E"), QColor(255, 165, 72), mvp);
    drawProjectedLabel(painter, m_state.skyNorthDirection * skyLength, tr("CIE N"), QColor(72, 242, 194), mvp);
    drawProjectedLabel(painter, m_state.skyUpDirection * skyLength, tr("CIE U"), QColor(194, 124, 255), mvp);
    drawProjectedLabel(painter, m_state.sunDirectionWorld * static_cast<float>(m_state.sphereRadius * 1.015), tr("Sun"), QColor(255, 215, 68), mvp);
    drawProjectedLabel(painter, m_state.cameraOriginWorld, tr("Camera"), QColor(255, 110, 96), mvp);
    drawProjectedLabel(painter, m_state.cameraOriginWorld + cameraAxisLength * m_state.cameraXAxisWorld, tr("Xc"), QColor(255, 110, 96), mvp);
    drawProjectedLabel(painter, m_state.cameraOriginWorld + cameraAxisLength * m_state.cameraYAxisWorld, tr("Yc"), QColor(100, 220, 255), mvp);
    drawProjectedLabel(painter, m_state.cameraOriginWorld + cameraAxisLength * m_state.cameraForwardWorld, tr("Zc / Forward"), QColor(96, 255, 118), mvp);
    painter.setPen(QColor(220, 224, 232));
    painter.drawText(10, 18, tr("World ENU -> finite sky sphere -> CIE ENU"));
    painter.drawText(10, 36, tr("Mouse: left-drag orbit, wheel zoom"));
}

void SkySceneWidget::drawProjectedLabel(QPainter& painter, const QVector3D& point, const QString& text, const QColor& color, const QMatrix4x4& mvp) const
{
    const QPointF position = projectToWidget(point, mvp);
    if (position.x() < -1000.0 || position.y() < -1000.0)
    {
        return;
    }
    painter.setPen(color);
    painter.drawText(position + QPointF(4.0, -4.0), text);
}

QVector3D SkySceneWidget::normalizedOr(const QVector3D& value, const QVector3D& fallback)
{
    if (value.lengthSquared() < 1.0e-12f)
    {
        return fallback.normalized();
    }
    return value.normalized();
}
