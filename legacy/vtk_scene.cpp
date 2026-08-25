#include "vtk_scene.h"
#include "CameraTransform.h"

#include <vtkRenderer.h>
#include <vtkRenderWindow.h>
#include <vtkSphereSource.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkAxesActor.h>
#include <vtkCamera.h>
#include <vtkLineSource.h>
#include <vtkTubeFilter.h>
#include <vtkPoints.h>
#include <vtkCellArray.h>
#include <vtkPolyData.h>
#include <vtkTexture.h>
#include <vtkImageData.h>
#include <vtkPointData.h>
#include <vtkFloatArray.h>
#include <vtkMatrix4x4.h>
#include <vtkTransform.h>

#include <QImage>
#include <QPainter>

#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace {

static bool raySphereIntersectionPoint(
    const double origin[3],
    const double dir[3],
    double hit[3])
{
    const double a = dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2];
    const double b = 2.0 * (origin[0] * dir[0] + origin[1] * dir[1] + origin[2] * dir[2]);
    const double c = origin[0] * origin[0] + origin[1] * origin[1] + origin[2] * origin[2] - 1.0;
    const double disc = b * b - 4.0 * a * c;
    if (disc < 0.0) return false;

    const double sqrtDisc = std::sqrt(disc);
    const double t1 = (-b - sqrtDisc) / (2.0 * a);
    const double t2 = (-b + sqrtDisc) / (2.0 * a);
    const double t = (t1 > 1e-6) ? t1 : ((t2 > 1e-6) ? t2 : -1.0);
    if (t <= 1e-6) return false;

    hit[0] = origin[0] + t * dir[0];
    hit[1] = origin[1] + t * dir[1];
    hit[2] = origin[2] + t * dir[2];
    return true;
}

static vtkSmartPointer<vtkActor> createLineSegment(
    const double p1[3], const double p2[3],
    double r, double g, double b,
    double width = 2.0)
{
    vtkSmartPointer<vtkLineSource> line = vtkSmartPointer<vtkLineSource>::New();
    line->SetPoint1(p1[0], p1[1], p1[2]);
    line->SetPoint2(p2[0], p2[1], p2[2]);

    vtkSmartPointer<vtkTubeFilter> tube = vtkSmartPointer<vtkTubeFilter>::New();
    tube->SetInputConnection(line->GetOutputPort());
    tube->SetRadius(0.008);
    tube->SetNumberOfSides(6);

    vtkSmartPointer<vtkPolyDataMapper> mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    mapper->SetInputConnection(tube->GetOutputPort());

    vtkSmartPointer<vtkActor> actor = vtkSmartPointer<vtkActor>::New();
    actor->SetMapper(mapper);
    actor->GetProperty()->SetColor(r, g, b);
    actor->GetProperty()->SetLineWidth(width);
    return actor;
}

static vtkSmartPointer<vtkActor> createSphereActor(
    double cx, double cy, double cz,
    double radius,
    double r, double g, double b)
{
    vtkSmartPointer<vtkSphereSource> sphere = vtkSmartPointer<vtkSphereSource>::New();
    sphere->SetCenter(cx, cy, cz);
    sphere->SetRadius(radius);
    sphere->SetThetaResolution(20);
    sphere->SetPhiResolution(20);

    vtkSmartPointer<vtkPolyDataMapper> mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    mapper->SetInputConnection(sphere->GetOutputPort());

    vtkSmartPointer<vtkActor> actor = vtkSmartPointer<vtkActor>::New();
    actor->SetMapper(mapper);
    actor->GetProperty()->SetColor(r, g, b);
    return actor;
}

// Build a textured unit sphere directly in ENU world coordinates.
// The mesh seam is deliberately placed at the SOURCE panorama seam, so an
// arbitrary North-position offset never interpolates across u=0/1 incorrectly.
static vtkSmartPointer<vtkPolyData> buildENUPanoramaSphere(double northPanoramaDeg)
{
    constexpr int azimuthSegments = 160;
    constexpr int zenithSegments = 80;

    double northU = std::fmod(northPanoramaDeg, 360.0) / 360.0;
    if (northU < 0.0) northU += 1.0;
    const double offset = northU - 0.5;

    vtkSmartPointer<vtkPoints> points = vtkSmartPointer<vtkPoints>::New();
    vtkSmartPointer<vtkFloatArray> tcoords = vtkSmartPointer<vtkFloatArray>::New();
    tcoords->SetName("PanoramaUV");
    tcoords->SetNumberOfComponents(2);

    for (int j = 0; j <= zenithSegments; ++j) {
        const double v = static_cast<double>(j) / zenithSegments;
        const double theta = v * M_PI;
        const double horizontal = std::sin(theta);
        const double up = std::cos(theta);

        for (int i = 0; i <= azimuthSegments; ++i) {
            const double sourceU = static_cast<double>(i) / azimuthSegments;

            double worldU = sourceU - offset;
            worldU = std::fmod(worldU, 1.0);
            if (worldU < 0.0) worldU += 1.0;

            const double azimuth = worldU * 2.0 * M_PI - M_PI;
            const double east = horizontal * std::sin(azimuth);
            const double north = horizontal * std::cos(azimuth);

            points->InsertNextPoint(east, north, up);
            const float uv[2] = {
                static_cast<float>(sourceU),
                static_cast<float>(v)
            };
            tcoords->InsertNextTuple(uv);
        }
    }

    vtkSmartPointer<vtkCellArray> polys = vtkSmartPointer<vtkCellArray>::New();
    const int stride = azimuthSegments + 1;
    for (int j = 0; j < zenithSegments; ++j) {
        for (int i = 0; i < azimuthSegments; ++i) {
            const vtkIdType a = j * stride + i;
            const vtkIdType b = a + 1;
            const vtkIdType c = (j + 1) * stride + i + 1;
            const vtkIdType d = (j + 1) * stride + i;

            vtkIdType tri1[3] = { a, b, c };
            vtkIdType tri2[3] = { a, c, d };
            polys->InsertNextCell(3, tri1);
            polys->InsertNextCell(3, tri2);
        }
    }

    vtkSmartPointer<vtkPolyData> poly = vtkSmartPointer<vtkPolyData>::New();
    poly->SetPoints(points);
    poly->SetPolys(polys);
    poly->GetPointData()->SetTCoords(tcoords);
    return poly;
}

} // namespace

VTKSceneWidget::VTKSceneWidget(QWidget* parent)
    : QVTKOpenGLStereoWidget(parent)
    , m_cx(0.5), m_cy(0.2), m_cz(0.3)
    , m_yaw(30.0), m_pitch(20.0), m_roll(0.0)
    , m_hfov(90.0), m_vfov(60.0)
    , m_outW(800), m_outH(600)
    , m_northPanoramaDeg(180.0)
    , m_flipVertical(false)
{
    setupScene();
}

VTKSceneWidget::~VTKSceneWidget() = default;

void VTKSceneWidget::setupScene()
{
    m_renderer = vtkSmartPointer<vtkRenderer>::New();
    m_renderer->SetBackground(0.1, 0.1, 0.2);
    this->renderWindow()->AddRenderer(m_renderer);

    // Textured world sphere: native VTK XYZ now IS ENU XYZ.
    vtkSmartPointer<vtkPolyDataMapper> sphereMapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    sphereMapper->SetInputData(buildENUPanoramaSphere(m_northPanoramaDeg));

    m_sphereActor = vtkSmartPointer<vtkActor>::New();
    m_sphereActor->SetMapper(sphereMapper);
    m_sphereActor->GetProperty()->SetColor(1.0, 1.0, 1.0);
    m_sphereActor->GetProperty()->SetOpacity(0.65);
    m_sphereActor->GetProperty()->LightingOff();

    m_texture = vtkSmartPointer<vtkTexture>::New();
    m_texture->InterpolateOn();
    m_texture->RepeatOn();
    m_texture->MipmapOff();
    m_sphereActor->SetTexture(nullptr);
    m_renderer->AddActor(m_sphereActor);

    // ENU wire sphere. vtkSphereSource is already +Z-up, exactly matching ENU Up.
    vtkSmartPointer<vtkSphereSource> wireSphere = vtkSmartPointer<vtkSphereSource>::New();
    wireSphere->SetRadius(1.01);
    wireSphere->SetThetaResolution(48);
    wireSphere->SetPhiResolution(24);
    vtkSmartPointer<vtkPolyDataMapper> wireMapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    wireMapper->SetInputConnection(wireSphere->GetOutputPort());
    vtkSmartPointer<vtkActor> wireActor = vtkSmartPointer<vtkActor>::New();
    wireActor->SetMapper(wireMapper);
    wireActor->GetProperty()->SetColor(0.9, 0.9, 0.9);
    wireActor->GetProperty()->SetRepresentationToWireframe();
    m_renderer->AddActor(wireActor);

    // Large, fixed ENU world axes.
    m_worldAxes = vtkSmartPointer<vtkAxesActor>::New();
    m_worldAxes->SetTotalLength(1.35, 1.35, 1.35);
    m_worldAxes->SetXAxisLabelText("E");
    m_worldAxes->SetYAxisLabelText("N");
    m_worldAxes->SetZAxisLabelText("U");
    m_renderer->AddActor(m_worldAxes);

    // Small Camera-local axes.  They are transformed on every RPY/position update.
    m_cameraAxes = vtkSmartPointer<vtkAxesActor>::New();
    m_cameraAxes->SetTotalLength(0.28, 0.28, 0.28);
    m_cameraAxes->SetXAxisLabelText("Xc");
    m_cameraAxes->SetYAxisLabelText("Yc");
    m_cameraAxes->SetZAxisLabelText("Zc");
    m_renderer->AddActor(m_cameraAxes);

    m_renderer->GetActiveCamera()->SetPosition(2.5, 1.5, 2.0);
    m_renderer->GetActiveCamera()->SetFocalPoint(0.0, 0.0, 0.0);
    m_renderer->GetActiveCamera()->SetViewUp(0.0, 0.0, 1.0); // ENU Up
    m_renderer->ResetCameraClippingRange();

    m_roiActor = nullptr;
    m_cameraActor = nullptr;
    for (int i = 0; i < 4; ++i) {
        m_rayActors[i] = nullptr;
        m_rectEdges[i] = nullptr;
    }

    updateROIAndRay();
    updatePerspective();
}

void VTKSceneWidget::updateSphereGeometryForNorth()
{
    if (!m_sphereActor) return;
    vtkPolyDataMapper* mapper = vtkPolyDataMapper::SafeDownCast(m_sphereActor->GetMapper());
    if (!mapper) return;
    mapper->SetInputData(buildENUPanoramaSphere(m_northPanoramaDeg));
    mapper->Modified();
}

void VTKSceneWidget::setPanorama(const HDRImage& img)
{
    m_panorama = img;
    m_panoramaDisplay = PanoramaProcessor::toneMapForDisplay(m_panorama, 1.0f, 2.2f);

    if (m_panoramaDisplay.width > 0 && m_panoramaDisplay.height > 0) {
        m_textureImage = vtkSmartPointer<vtkImageData>::New();
        m_textureImage->SetDimensions(m_panoramaDisplay.width, m_panoramaDisplay.height, 1);
        m_textureImage->AllocateScalars(VTK_UNSIGNED_CHAR, 3);

        for (int y = 0; y < m_panoramaDisplay.height; ++y) {
            for (int x = 0; x < m_panoramaDisplay.width; ++x) {
                const sRGB& c = m_panoramaDisplay.at(x, y);
                unsigned char* pixel = static_cast<unsigned char*>(
                    m_textureImage->GetScalarPointer(x, y, 0));
                pixel[0] = static_cast<unsigned char>(c.r);
                pixel[1] = static_cast<unsigned char>(c.g);
                pixel[2] = static_cast<unsigned char>(c.b);
            }
        }

        m_textureImage->Modified();
        m_texture->SetInputData(m_textureImage);
        m_texture->SetColorModeToDirectScalars();
        m_texture->Modified();
        m_texture->Update();
        m_sphereActor->SetTexture(m_texture);
    }

    updateSphereGeometryForNorth();
    this->renderWindow()->Render();
    updatePerspective();
}

void VTKSceneWidget::setCameraParameters(
    double cx, double cy, double cz,
    double yaw, double pitch, double roll,
    double hfov, double vfov,
    int outW, int outH,
    double northPanoramaDeg,
    bool flipVertical)
{
    m_cx = cx; m_cy = cy; m_cz = cz;
    m_yaw = yaw; m_pitch = pitch; m_roll = roll;
    m_hfov = hfov; m_vfov = vfov;
    m_outW = outW; m_outH = outH;
    m_flipVertical = flipVertical;

    if (std::abs(m_northPanoramaDeg - northPanoramaDeg) > 1e-9) {
        m_northPanoramaDeg = northPanoramaDeg;
        updateSphereGeometryForNorth();
    }

    updateROIAndRay();
    updatePerspective();
    this->renderWindow()->Render();
}

void VTKSceneWidget::updatePerspective()
{
    if (m_panorama.width == 0) return;

    HDRImage perspectiveHDR = PanoramaProcessor::perspectiveFromPanorama(
        m_panorama,
        m_cx, m_cy, m_cz,
        m_yaw, m_pitch, m_roll,
        m_hfov, m_vfov,
        m_outW, m_outH,
        2,
        m_northPanoramaDeg,
        m_flipVertical);

    Image perspective = PanoramaProcessor::toneMapForDisplay(perspectiveHDR, 1.0f, 2.2f);
    QImage qimg(perspective.width, perspective.height, QImage::Format_RGB888);
    for (int y = 0; y < perspective.height; ++y) {
        for (int x = 0; x < perspective.width; ++x) {
            const sRGB& c = perspective.at(x, y);
            qimg.setPixel(x, y, qRgb(
                static_cast<int>(c.r),
                static_cast<int>(c.g),
                static_cast<int>(c.b)));
        }
    }

    emit perspectiveViewReady(qimg);
}

void VTKSceneWidget::updateROIAndRay()
{
    CameraTransform::RayContext rayCtx;
    const bool contextOk = CameraTransform::buildRayContext(
        m_cx, m_cy, m_cz,
        m_yaw, m_pitch, m_roll,
        m_hfov, m_vfov,
        m_outW, m_outH,
        m_flipVertical,
        rayCtx);

    const std::pair<double, double> corners[4] = {
        {0.0, 0.0},
        {m_outW - 1.0, 0.0},
        {0.0, m_outH - 1.0},
        {m_outW - 1.0, m_outH - 1.0}
    };

    double hitPoints[4][3]{};
    bool ok = contextOk;

    if (ok) {
        for (int i = 0; i < 4; ++i) {
            double dirENU[3];
            if (!CameraTransform::pixelToENUDirection(
                    rayCtx,
                    corners[i].first,
                    corners[i].second,
                    dirENU) ||
                !raySphereIntersectionPoint(rayCtx.originENU, dirENU, hitPoints[i]))
            {
                ok = false;
                break;
            }
        }
    }

    if (m_cameraActor) m_renderer->RemoveActor(m_cameraActor);
    for (int i = 0; i < 4; ++i) {
        if (m_rayActors[i]) m_renderer->RemoveActor(m_rayActors[i]);
        if (m_rectEdges[i]) m_renderer->RemoveActor(m_rectEdges[i]);
    }
    if (m_roiActor) m_renderer->RemoveActor(m_roiActor);

    // Camera marker location: UI X/Y/Z are local translations, therefore the
    // displayed world location is rayCtx.originENU.
    m_cameraActor = createSphereActor(
        rayCtx.originENU[0], rayCtx.originENU[1], rayCtx.originENU[2],
        0.045, 1.0, 0.2, 0.2);
    m_renderer->AddActor(m_cameraActor);

    // Transform the small Xc/Yc/Zc axes into ENU and translate them to the
    // camera's ENU world position.
    vtkSmartPointer<vtkMatrix4x4> cameraMatrix = vtkSmartPointer<vtkMatrix4x4>::New();
    cameraMatrix->Identity();
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c)
            cameraMatrix->SetElement(r, c, rayCtx.cameraToENU[r][c]);
        cameraMatrix->SetElement(r, 3, rayCtx.originENU[r]);
    }

    vtkSmartPointer<vtkTransform> cameraTransform = vtkSmartPointer<vtkTransform>::New();
    cameraTransform->SetMatrix(cameraMatrix);
    m_cameraAxes->SetUserTransform(cameraTransform);

    if (ok) {
        for (int i = 0; i < 4; ++i) {
            m_rayActors[i] = createLineSegment(
                rayCtx.originENU, hitPoints[i],
                0.2, 1.0, 0.2, 2.0);
            m_renderer->AddActor(m_rayActors[i]);
        }

        const int order[5] = {0, 1, 3, 2, 0};
        for (int i = 0; i < 4; ++i) {
            m_rectEdges[i] = createLineSegment(
                hitPoints[order[i]], hitPoints[order[i + 1]],
                0.2, 1.0, 0.2, 2.5);
            m_renderer->AddActor(m_rectEdges[i]);
        }
    }

    m_renderer->ResetCameraClippingRange();
    this->renderWindow()->Render();
}

PanoramaLabel::PanoramaLabel(QWidget* parent)
    : QLabel(parent)
    , m_hasCorners(false)
    , m_northDirectionDeg(180.0)
{
    setAlignment(Qt::AlignCenter);
    setMinimumSize(400, 200);
    setStyleSheet("border: 1px solid gray; background-color: #2a2a2a;");
}

void PanoramaLabel::setPanoramaImage(const Image& img)
{
    QImage qimg(img.width, img.height, QImage::Format_RGB888);
    for (int y = 0; y < img.height; ++y) {
        for (int x = 0; x < img.width; ++x) {
            const sRGB& c = img.at(x, y);
            qimg.setPixel(x, y, qRgb(c.r, c.g, c.b));
        }
    }
    m_pixmap = QPixmap::fromImage(qimg);
    update();
}

void PanoramaLabel::setCorners(const std::vector<QPointF>& corners)
{
    if (corners.size() >= 4) {
        m_corners = corners;
        m_hasCorners = true;
    }
    else {
        m_hasCorners = false;
    }
    update();
}

void PanoramaLabel::clearCorners()
{
    m_hasCorners = false;
    update();
}

void PanoramaLabel::setNorthDirectionDegrees(double degrees)
{
    m_northDirectionDeg = std::fmod(degrees, 360.0);
    if (m_northDirectionDeg < 0.0)
        m_northDirectionDeg += 360.0;
    update();
}

void PanoramaLabel::paintEvent(QPaintEvent* event)
{
    if (m_pixmap.isNull()) {
        QLabel::paintEvent(event);
        return;
    }

    QPainter painter(this);
    // 缩放以适应控件，保持宽高比
    QPixmap scaled = m_pixmap.scaled(size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    int x = (width() - scaled.width()) / 2;
    int y = (height() - scaled.height()) / 2;
    painter.drawPixmap(x, y, scaled);

    // Visualize the source-panorama geographic directions.  This follows the
    // same convention used by PanoramaProcessor::applyNorthPanoramaOffset():
    // northDirectionDeg is the horizontal SOURCE-image position of North,
    // where 0/360 is the panorama seam and 180 is the image center.
    {
        const double imgW = static_cast<double>(scaled.width());
        const double imgH = static_cast<double>(scaled.height());
        const char* labels[4] = { "N", "E", "S", "W" };

        QPen markerPen(QColor(255, 220, 40, 220), 1.5, Qt::DashLine);
        painter.setPen(markerPen);

        QFont f = painter.font();
        f.setBold(true);
        painter.setFont(f);

        for (int i = 0; i < 4; ++i) {
            double deg = std::fmod(m_northDirectionDeg + i * 90.0, 360.0);
            if (deg < 0.0) deg += 360.0;
            const double u = deg / 360.0;
            const double px = x + u * imgW;

            painter.drawLine(QPointF(px, y), QPointF(px, y + imgH));
            painter.drawText(QPointF(px + 4.0, y + 18.0), labels[i]);
        }
    }

    if (m_hasCorners && m_corners.size() >= 3) {
        QPolygonF poly;
        double imgW = scaled.width(), imgH = scaled.height();
        for (const auto& uv : m_corners) {
            poly << QPointF(uv.x() * imgW + x, uv.y() * imgH + y);
        }
        painter.setPen(QPen(Qt::red, 2));
        painter.drawPolyline(poly); // 首尾不自动闭合，可以根据需要添加闭合线
        if (poly.size() > 2) painter.drawLine(poly.last(), poly.first());
    }
}