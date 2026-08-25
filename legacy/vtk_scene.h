#ifndef VTK_SCENE_WIDGET_H
#define VTK_SCENE_WIDGET_H

#include <QVTKOpenGLStereoWidget.h>
#include <vtkSmartPointer.h>
#include <vtkActor.h>
#include "panorama_processor.h"
#include <QLabel>

class vtkRenderer;
class vtkSphereSource;
class vtkTexture;
class vtkImageData;
class vtkAxesActor;

class VTKSceneWidget : public QVTKOpenGLStereoWidget
{
    Q_OBJECT

public:
    explicit VTKSceneWidget(QWidget* parent = nullptr);
    ~VTKSceneWidget();

    void setPanorama(const HDRImage& img);

    // cx/cy/cz are Camera-local translations along the CURRENT Xc/Yc/Zc axes.
    // yaw/pitch/roll keep the traditional camera Euler-control convention.
    void setCameraParameters(
        double cx, double cy, double cz,
        double yaw, double pitch, double roll,
        double hfov, double vfov,
        int outW, int outH,
        double northPanoramaDeg = 180.0,
        bool flipVertical = false);

signals:
    void perspectiveViewReady(const QImage& image);

private:
    void setupScene();
    void updatePerspective();
    void updateROIAndRay();
    void updateSphereGeometryForNorth();

    vtkSmartPointer<vtkRenderer> m_renderer;
    vtkSmartPointer<vtkSphereSource> m_sphereSource;
    vtkSmartPointer<vtkActor> m_sphereActor;
    vtkSmartPointer<vtkActor> m_roiActor;
    vtkSmartPointer<vtkActor> m_cameraActor;
    vtkSmartPointer<vtkActor> m_rayActors[4];
    vtkSmartPointer<vtkActor> m_rectEdges[4];
    vtkSmartPointer<vtkTexture> m_texture;
    vtkSmartPointer<vtkImageData> m_textureImage;

    // Large fixed ENU world axes and small moving Camera-local axes.
    vtkSmartPointer<vtkAxesActor> m_worldAxes;
    vtkSmartPointer<vtkAxesActor> m_cameraAxes;

    // UI camera local translations and Euler controls.
    double m_cx, m_cy, m_cz;
    double m_yaw, m_pitch, m_roll;
    double m_hfov, m_vfov;
    int m_outW, m_outH;
    double m_northPanoramaDeg;
    bool m_flipVertical;

    HDRImage m_panorama;
    Image m_panoramaDisplay;
};

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
    bool m_hasCorners;
    std::vector<QPointF> m_corners;
    double m_northDirectionDeg;
};

#endif // VTK_SCENE_WIDGET_H
