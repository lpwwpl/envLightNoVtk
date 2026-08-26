#pragma once

#include <QOpenGLWidget>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLBuffer>
#include <QOpenGLVertexArrayObject>
#include <QImage>
#include <QLabel>
#include <QMatrix4x4>
#include <QPoint>
#include <QVector2D>
#include <QVector3D>
#include <QVector4D>

#include <memory>
#include <vector>

#include "CameraTransform.h"
#include "panorama_processor.h"

class QOpenGLShaderProgram;
class QOpenGLTexture;
class QMouseEvent;
class QWheelEvent;

// OpenGL replacement for the old VTKSceneWidget.
// World space is ENU: +X=East, +Y=North, +Z=Up.
// Camera keeps the traditional local XYZ convention, +Zc=Forward.
class OpenGLSceneWidget final
    : public QOpenGLWidget
    , protected QOpenGLFunctions_3_3_Core
{
    Q_OBJECT

public:
    explicit OpenGLSceneWidget(QWidget* parent = nullptr);
    ~OpenGLSceneWidget() override;

    void setPanorama(const HDRImage& img);

    // cx/cy/cz are LOCAL translations along current Xc/Yc/Zc.
    // yaw/pitch/roll keep the historical camera Euler convention.
    void setCameraParameters(
        double cx, double cy, double cz,
        double yaw, double pitch, double roll,
        double hfov, double vfov,
        int outW, int outH,
        //double northPanoramaDeg = 180.0,
		const PanoramaBasis& panoramaBasis,
        bool flipVertical = false);

	// World ENU永不动它
	//修改Panorama ENU
	void setPanoramaBasis(const PanoramaBasis& basis);
	//不要删除 World ENU，再增加 Panorama ENU
	void drawPanoramaAxes(const QMatrix4x4& mvp);


signals:
    void perspectiveViewReady(const QImage& image);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    struct SphereVertex {
        QVector3D position;
        QVector2D uv;
    };

    void createShaders();
    void rebuildSphereMesh();
    void rebuildWireSphere();
    void uploadPanoramaTexture();
    void updateSceneGeometry();
    void updatePerspective();

    void drawTexturedSphere(const QMatrix4x4& mvp);
    void drawWireSphere(const QMatrix4x4& mvp);
    void drawWorldAxes(const QMatrix4x4& mvp);
    void drawCameraAxes(const QMatrix4x4& mvp);
    void drawFrustumAndROI(const QMatrix4x4& mvp);
    void drawCameraMarker(const QMatrix4x4& mvp);

    void drawLines(
        const std::vector<QVector3D>& vertices,
        const QVector4D& color,
        const QMatrix4x4& mvp,
        GLenum primitive = GL_LINES,
        float lineWidth = 1.0f);

    void drawPoints(
        const std::vector<QVector3D>& vertices,
        const QVector4D& color,
        const QMatrix4x4& mvp,
        float pointSize);

    QPointF projectToWidget(const QVector3D& p, const QMatrix4x4& mvp) const;
    void paintAxisLabels(const QMatrix4x4& mvp);

    static bool raySphereIntersectionPoint(
        const double origin[3],
        const double dir[3],
        QVector3D& hit);

private:
    std::unique_ptr<QOpenGLShaderProgram> m_textureProgram;
    std::unique_ptr<QOpenGLShaderProgram> m_colorProgram;
    std::unique_ptr<QOpenGLTexture> m_panoramaTexture;

    QOpenGLVertexArrayObject m_sphereVao;
    QOpenGLBuffer m_sphereVbo;
    QOpenGLBuffer m_sphereIbo { QOpenGLBuffer::IndexBuffer };
    int m_sphereIndexCount = 0;

    QOpenGLVertexArrayObject m_lineVao;
    QOpenGLBuffer m_lineVbo;

    std::vector<QVector3D> m_wireVertices;

    HDRImage m_panorama;
    QImage m_panoramaTextureImage;
    bool m_textureDirty = false;
    bool m_sphereDirty = true;
    bool m_glReady = false;
	bool m_wireDirty = true;

    double m_cx = 0.5;
    double m_cy = 0.2;
    double m_cz = 0.3;
    double m_yaw = 30.0;
    double m_pitch = 20.0;
    double m_roll = 0.0;
    double m_hfov = 90.0;
    double m_vfov = 60.0;
    int m_outW = 800;
    int m_outH = 600;
    double m_northPanoramaDeg = 180.0;
    bool m_flipVertical = false;

    CameraTransform::RayContext m_rayCtx;
    bool m_rayContextValid = false;
    bool m_hitValid = false;
    QVector3D m_hitPoints[4];

    // Independent orbit camera used only to inspect the 3D diagnostic scene.
    float m_viewYaw = 35.0f;
    float m_viewPitch = 25.0f;
    float m_viewDistance = 3.2f;
    QPoint m_lastMouse;

	PanoramaBasis m_panoramaBasis;
};

// 2D panorama preview, retained from the former vtk_scene implementation.
class PanoramaLabel : public QLabel
{
    Q_OBJECT
public:
    explicit PanoramaLabel(QWidget* parent = nullptr);
    void setPanoramaImage(const Image& img);
    void setCorners(const std::vector<QPointF>& corners);
    void clearCorners();
    void setNorthDirectionDegrees(double degrees);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QPixmap m_pixmap;
    bool m_hasCorners = false;
    std::vector<QPointF> m_corners;
    double m_northDirectionDeg = 180.0;
};
