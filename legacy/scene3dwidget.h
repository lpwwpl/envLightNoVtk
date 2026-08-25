#ifndef SCENE3DWIDGET_H
#define SCENE3DWIDGET_H

#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QOpenGLBuffer>
#include <QVector3D>
#include <vector>

class Scene3DWidget : public QOpenGLWidget, protected QOpenGLFunctions
{
    Q_OBJECT
public:
    explicit Scene3DWidget(QWidget* parent = nullptr);
    ~Scene3DWidget();

    void setCameraParams(double cx, double cy, double cz,
        double yaw, double pitch, double roll,
        double hfov, double vfov,
        int outW, int outH);
    void setROI(double u0, double u1, double v0, double v1);
    void setShowROI(bool show) { m_showROI = show; update(); }

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

private:
    void setupSphere();
    void drawSphere();
    void drawCamera();
    void drawFrustum();
    void drawROIOnSphere();
    QVector3D uvToWorld(double u, double v) const;
    void drawWireCube(float size);

    std::vector<float> m_sphereVertices;
    std::vector<unsigned int> m_sphereIndices;
    QOpenGLBuffer m_vbo, m_ibo;

    double m_cx, m_cy, m_cz;
    double m_yaw, m_pitch, m_roll;
    double m_hfov, m_vfov;
    int m_outW, m_outH;
    bool m_paramsValid;

    QVector3D m_corners[4];
    bool m_cornersValid;

    double m_roi_u0, m_roi_u1, m_roi_v0, m_roi_v1;
    bool m_roiValid;
    bool m_showROI;
};

#endif // SCENE3DWIDGET_H