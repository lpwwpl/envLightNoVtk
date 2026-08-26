#include "OpenGLSceneWidget.h"

#include <QMouseEvent>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QPainter>
#include <QPaintEvent>
#include <QPixmap>
#include <QWheelEvent>
#include <QPolygonF>
#include <QPen>
#include <QFont>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace {
constexpr double kPi = 3.14159265358979323846;

inline double wrap360(double degrees)
{
    degrees = std::fmod(degrees, 360.0);
    if (degrees < 0.0) degrees += 360.0;
    return degrees;
}

QImage toQImage(const Image& img)
{
    if (img.width <= 0 || img.height <= 0)
        return {};

    QImage qimg(img.width, img.height, QImage::Format_RGB888);
    for (int y = 0; y < img.height; ++y) {
        auto* row = qimg.scanLine(y);
        for (int x = 0; x < img.width; ++x) {
            const sRGB& c = img.at(x, y);
            row[x * 3 + 0] = static_cast<unsigned char>(std::clamp(c.r, 0.0, 255.0));
            row[x * 3 + 1] = static_cast<unsigned char>(std::clamp(c.g, 0.0, 255.0));
            row[x * 3 + 2] = static_cast<unsigned char>(std::clamp(c.b, 0.0, 255.0));
        }
    }
    return qimg;
}

inline QVector3D panoramaLocalToWorld(
	double east,
	double north,
	double up,
	const PanoramaBasis& basis)
{
	return QVector3D(
		static_cast<float>(
			east  * basis.east[0]
			+ north * basis.north[0]
			+ up * basis.up[0]),

		static_cast<float>(
			east  * basis.east[1]
			+ north * basis.north[1]
			+ up * basis.up[1]),

		static_cast<float>(
			east  * basis.east[2]
			+ north * basis.north[2]
			+ up * basis.up[2]));
}

inline QVector3D panoramaEast(
	const PanoramaBasis& basis)
{
	return QVector3D(
		static_cast<float>(basis.east[0]),
		static_cast<float>(basis.east[1]),
		static_cast<float>(basis.east[2]));
}

inline QVector3D panoramaNorth(
	const PanoramaBasis& basis)
{
	return QVector3D(
		static_cast<float>(basis.north[0]),
		static_cast<float>(basis.north[1]),
		static_cast<float>(basis.north[2]));
}

inline QVector3D panoramaUp(
	const PanoramaBasis& basis)
{
	return QVector3D(
		static_cast<float>(basis.up[0]),
		static_cast<float>(basis.up[1]),
		static_cast<float>(basis.up[2]));
}
}

OpenGLSceneWidget::OpenGLSceneWidget(QWidget* parent)
    : QOpenGLWidget(parent)
    , m_sphereVbo(QOpenGLBuffer::VertexBuffer)
    , m_lineVbo(QOpenGLBuffer::VertexBuffer)
{
    setMinimumSize(400, 260);
    setFocusPolicy(Qt::StrongFocus);
    rebuildWireSphere();
    updateSceneGeometry();
}

OpenGLSceneWidget::~OpenGLSceneWidget()
{
    if (!context()) return;

    makeCurrent();
    m_panoramaTexture.reset();
    m_sphereVao.destroy();
    m_sphereVbo.destroy();
    m_sphereIbo.destroy();
    m_lineVao.destroy();
    m_lineVbo.destroy();
    m_textureProgram.reset();
    m_colorProgram.reset();
    doneCurrent();
}

void OpenGLSceneWidget::setPanorama(const HDRImage& img)
{
    m_panorama = img;
    const Image display = PanoramaProcessor::toneMapForDisplay(m_panorama, 1.0f, 2.2f);
    m_panoramaTextureImage = toQImage(display);
    m_textureDirty = true;
    update();
    updatePerspective();
}

void OpenGLSceneWidget::setCameraParameters(
    double cx, double cy, double cz,
    double yaw, double pitch, double roll,
    double hfov, double vfov,
    int outW, int outH,
    //double northPanoramaDeg,
	const PanoramaBasis& panoramaBasis,
    bool flipVertical)
{
    m_cx = cx;
    m_cy = cy;
    m_cz = cz;
    m_yaw = yaw;
    m_pitch = pitch;
    m_roll = roll;
    m_hfov = hfov;
    m_vfov = vfov;
    m_outW = outW;
    m_outH = outH;
    m_flipVertical = flipVertical;

    //const double normalizedNorth = wrap360(northPanoramaDeg);
    //if (std::abs(normalizedNorth - m_northPanoramaDeg) > 1e-9) {
    //    m_northPanoramaDeg = normalizedNorth;
    //    m_sphereDirty = true;
    //}
	bool basisChanged = false;

	for (int i = 0; i < 3; ++i)
	{
		if (std::abs(
			m_panoramaBasis.east[i]
			- panoramaBasis.east[i]) > 1e-8 ||
			std::abs(
				m_panoramaBasis.north[i]
				- panoramaBasis.north[i]) > 1e-8 ||
			std::abs(
				m_panoramaBasis.up[i]
				- panoramaBasis.up[i]) > 1e-8)
		{
			basisChanged = true;
			break;
		}
	}

	if (basisChanged)
	{
		m_panoramaBasis = panoramaBasis;

		// Sphere vertices carry source UV, so changing the panorama
		// coordinate system requires rebuilding their world positions.
		m_sphereDirty = true;
		m_wireDirty = true;
	}

    updateSceneGeometry();
    updatePerspective();
    update();
}

void OpenGLSceneWidget::initializeGL()
{
    initializeOpenGLFunctions();

    glClearColor(0.06f, 0.07f, 0.11f, 1.0f);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_CULL_FACE);

    createShaders();

    m_sphereVao.create();
    m_sphereVbo.create();
    m_sphereIbo.create();
    m_lineVao.create();
    m_lineVbo.create();

    m_glReady = true;
    rebuildSphereMesh();
    if (m_textureDirty)
        uploadPanoramaTexture();
}

void OpenGLSceneWidget::resizeGL(int w, int h)
{
    glViewport(0, 0, w, h);
}

void OpenGLSceneWidget::paintGL()
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (m_sphereDirty)
        rebuildSphereMesh();
    if (m_textureDirty)
        uploadPanoramaTexture();
	if (m_wireDirty)
		rebuildWireSphere();
    const float yaw = qDegreesToRadians(m_viewYaw);
    const float pitch = qDegreesToRadians(m_viewPitch);
    const float cp = std::cos(pitch);

    const QVector3D eye(
        m_viewDistance * cp * std::cos(yaw),
        m_viewDistance * cp * std::sin(yaw),
        m_viewDistance * std::sin(pitch));

    QMatrix4x4 view;
    view.lookAt(eye, QVector3D(0, 0, 0), QVector3D(0, 0, 1));

    QMatrix4x4 projection;
    const float aspect = height() > 0 ? static_cast<float>(width()) / height() : 1.0f;
    projection.perspective(45.0f, aspect, 0.05f, 20.0f);

    const QMatrix4x4 mvp = projection * view;

    drawTexturedSphere(mvp);
    drawWireSphere(mvp);
	// 固定世界 ENU
    drawWorldAxes(mvp);

	// 随全景图旋转的 ENU
	drawPanoramaAxes(mvp);


    drawCameraAxes(mvp);
    drawFrustumAndROI(mvp);
    drawCameraMarker(mvp);

    paintAxisLabels(mvp);
}

void OpenGLSceneWidget::mousePressEvent(QMouseEvent* event)
{
    m_lastMouse = event->pos();
    QOpenGLWidget::mousePressEvent(event);
}

void OpenGLSceneWidget::mouseMoveEvent(QMouseEvent* event)
{
    const QPoint delta = event->pos() - m_lastMouse;
    m_lastMouse = event->pos();

    if (event->buttons() & Qt::LeftButton) {
        m_viewYaw += delta.x() * 0.45f;
        m_viewPitch += delta.y() * 0.35f;
        m_viewPitch = std::clamp(m_viewPitch, -85.0f, 85.0f);
        update();
    }

    QOpenGLWidget::mouseMoveEvent(event);
}

void OpenGLSceneWidget::wheelEvent(QWheelEvent* event)
{
    const float steps = event->angleDelta().y() / 120.0f;
    m_viewDistance *= std::pow(0.88f, steps);
    m_viewDistance = std::clamp(m_viewDistance, 1.4f, 8.0f);
    update();
    event->accept();
}

void OpenGLSceneWidget::createShaders()
{
    m_textureProgram = std::make_unique<QOpenGLShaderProgram>();
    m_textureProgram->addShaderFromSourceCode(QOpenGLShader::Vertex, R"(
        #version 330 core
        layout(location = 0) in vec3 aPos;
        layout(location = 1) in vec2 aUV;
        uniform mat4 uMVP;
        out vec2 vUV;
        void main() {
            gl_Position = uMVP * vec4(aPos, 1.0);
            vUV = aUV;
        }
    )");
    m_textureProgram->addShaderFromSourceCode(QOpenGLShader::Fragment, R"(
        #version 330 core
        in vec2 vUV;
        uniform sampler2D uPanorama;
        uniform float uOpacity;
        out vec4 FragColor;
        void main() {
            vec3 rgb = texture(uPanorama, vUV).rgb;
            FragColor = vec4(rgb, uOpacity);
        }
    )");
    m_textureProgram->link();

    m_colorProgram = std::make_unique<QOpenGLShaderProgram>();
    m_colorProgram->addShaderFromSourceCode(QOpenGLShader::Vertex, R"(
        #version 330 core
        layout(location = 0) in vec3 aPos;
        uniform mat4 uMVP;
        void main() {
            gl_Position = uMVP * vec4(aPos, 1.0);
        }
    )");
    m_colorProgram->addShaderFromSourceCode(QOpenGLShader::Fragment, R"(
        #version 330 core
        uniform vec4 uColor;
        out vec4 FragColor;
        void main() {
            FragColor = uColor;
        }
    )");
    m_colorProgram->link();
}

void OpenGLSceneWidget::rebuildSphereMesh()
{
    if (!m_glReady) {
        m_sphereDirty = true;
        return;
    }

    constexpr int azimuthSegments = 160;
    constexpr int zenithSegments = 80;

    std::vector<SphereVertex> vertices;
    std::vector<std::uint32_t> indices;
    vertices.reserve((azimuthSegments + 1) * (zenithSegments + 1));
    indices.reserve(azimuthSegments * zenithSegments * 6);

    //const double northU = wrap360(m_northPanoramaDeg) / 360.0;
    //const double offset = northU - 0.5;

    // The seam follows source panorama u=0/1. Geometry is rotated in ENU so
    // source North appears at northPanoramaDeg while UV remains continuous.
    //for (int j = 0; j <= zenithSegments; ++j) {
    //    const double sourceV = static_cast<double>(j) / zenithSegments;
    //    const double theta = sourceV * kPi;
    //    const double horizontal = std::sin(theta);
    //    const double up = std::cos(theta);

    //    for (int i = 0; i <= azimuthSegments; ++i) {
    //        const double sourceU = static_cast<double>(i) / azimuthSegments;

    //        double worldU = sourceU - offset;
    //        worldU = std::fmod(worldU, 1.0);
    //        if (worldU < 0.0) worldU += 1.0;

    //        const double azimuth = worldU * 2.0 * kPi - kPi;
    //        const double east = horizontal * std::sin(azimuth);
    //        const double north = horizontal * std::cos(azimuth);

    //        vertices.push_back({
    //            QVector3D(static_cast<float>(east), static_cast<float>(north), static_cast<float>(up)),
    //            QVector2D(static_cast<float>(sourceU), static_cast<float>(sourceV))
    //        });
    //    }
    //}
	for (int j = 0; j <= zenithSegments; ++j)
	{
		const double sourceV =
			static_cast<double>(j) /
			zenithSegments;

		const double theta =
			sourceV * kPi;

		const double horizontal =
			std::sin(theta);

		const double localUp =
			std::cos(theta);

		for (int i = 0; i <= azimuthSegments; ++i)
		{
			const double sourceU =
				static_cast<double>(i) /
				azimuthSegments;

			const double azimuth =
				sourceU * 2.0 * kPi - kPi;

			// Source panorama-local coordinate.
			const double localEast =
				horizontal * std::sin(azimuth);

			const double localNorth =
				horizontal * std::cos(azimuth);

			// Panorama local -> ENU world.
			QVector3D worldPosition(
				static_cast<float>(
					localEast  * m_panoramaBasis.east[0]
					+ localNorth * m_panoramaBasis.north[0]
					+ localUp * m_panoramaBasis.up[0]),

				static_cast<float>(
					localEast  * m_panoramaBasis.east[1]
					+ localNorth * m_panoramaBasis.north[1]
					+ localUp * m_panoramaBasis.up[1]),

				static_cast<float>(
					localEast  * m_panoramaBasis.east[2]
					+ localNorth * m_panoramaBasis.north[2]
					+ localUp * m_panoramaBasis.up[2])
			);

			vertices.push_back(
				{
					worldPosition,
					QVector2D(
						static_cast<float>(sourceU),
						static_cast<float>(sourceV))
				});
		}
	}

    const int stride = azimuthSegments + 1;
    for (int j = 0; j < zenithSegments; ++j) {
        for (int i = 0; i < azimuthSegments; ++i) {
            const std::uint32_t a = static_cast<std::uint32_t>(j * stride + i);
            const std::uint32_t b = a + 1;
            const std::uint32_t c = static_cast<std::uint32_t>((j + 1) * stride + i + 1);
            const std::uint32_t d = static_cast<std::uint32_t>((j + 1) * stride + i);
            indices.insert(indices.end(), {a, b, c, a, c, d});
        }
    }

    m_sphereVao.bind();
    m_sphereVbo.bind();
    m_sphereVbo.setUsagePattern(QOpenGLBuffer::StaticDraw);
    m_sphereVbo.allocate(vertices.data(), static_cast<int>(vertices.size() * sizeof(SphereVertex)));

    m_sphereIbo.bind();
    m_sphereIbo.setUsagePattern(QOpenGLBuffer::StaticDraw);
    m_sphereIbo.allocate(indices.data(), static_cast<int>(indices.size() * sizeof(std::uint32_t)));

    m_textureProgram->bind();
    m_textureProgram->enableAttributeArray(0);
    m_textureProgram->setAttributeBuffer(
        0, GL_FLOAT, offsetof(SphereVertex, position), 3, sizeof(SphereVertex));
    m_textureProgram->enableAttributeArray(1);
    m_textureProgram->setAttributeBuffer(
        1, GL_FLOAT, offsetof(SphereVertex, uv), 2, sizeof(SphereVertex));
    m_textureProgram->release();

    m_sphereIbo.release();
    m_sphereVbo.release();
    m_sphereVao.release();

    m_sphereIndexCount = static_cast<int>(indices.size());
    m_sphereDirty = false;
}

void OpenGLSceneWidget::rebuildWireSphere()
{
    m_wireVertices.clear();
    constexpr int segments = 96;
	constexpr double R = 1.006;

	//更新经纬度（非必要的部分），可有可无
	// ---------------------------------
	// 纬线
	// ---------------------------------
	for (int altDeg = -75;
		altDeg <= 75;
		altDeg += 15)
	{
		const double alt =
			altDeg * kPi / 180.0;

		const double horizontal =
			std::cos(alt) * R;

		const double localUp =
			std::sin(alt) * R;

		for (int i = 0; i < segments; ++i)
		{
			const double a0 =
				2.0 * kPi *
				i / segments;

			const double a1 =
				2.0 * kPi *
				(i + 1) / segments;

			const QVector3D p0 =
				panoramaLocalToWorld(
					horizontal * std::sin(a0),
					horizontal * std::cos(a0),
					localUp,
					m_panoramaBasis);

			const QVector3D p1 =
				panoramaLocalToWorld(
					horizontal * std::sin(a1),
					horizontal * std::cos(a1),
					localUp,
					m_panoramaBasis);

			m_wireVertices.push_back(p0);
			m_wireVertices.push_back(p1);
		}
	}

	// ---------------------------------
	// 经线
	// ---------------------------------
	for (int azDeg = 0;
		azDeg < 360;
		azDeg += 15)
	{
		const double az =
			azDeg * kPi / 180.0;

		for (int i = 0;
			i < segments / 2;
			++i)
		{
			const double t0 =
				-kPi / 2.0 +
				kPi * i /
				(segments / 2);

			const double t1 =
				-kPi / 2.0 +
				kPi * (i + 1) /
				(segments / 2);

			const double h0 =
				std::cos(t0) * R;

			const double h1 =
				std::cos(t1) * R;

			const QVector3D p0 =
				panoramaLocalToWorld(
					h0 * std::sin(az),
					h0 * std::cos(az),
					std::sin(t0) * R,
					m_panoramaBasis);

			const QVector3D p1 =
				panoramaLocalToWorld(
					h1 * std::sin(az),
					h1 * std::cos(az),
					std::sin(t1) * R,
					m_panoramaBasis);

			m_wireVertices.push_back(p0);
			m_wireVertices.push_back(p1);
		}
		m_wireDirty = false;
	}
	//原先经纬度不更新
    // Altitude circles, excluding the poles and equator duplicated by meridians.
    //for (int altDeg = -75; altDeg <= 75; altDeg += 15) {
    //    const double alt = altDeg * kPi / 180.0;
    //    const double r = std::cos(alt) * 1.006;
    //    const double z = std::sin(alt) * 1.006;
    //    for (int i = 0; i < segments; ++i) {
    //        const double a0 = 2.0 * kPi * i / segments;
    //        const double a1 = 2.0 * kPi * (i + 1) / segments;
    //        m_wireVertices.emplace_back(
    //            static_cast<float>(r * std::sin(a0)),
    //            static_cast<float>(r * std::cos(a0)),
    //            static_cast<float>(z));
    //        m_wireVertices.emplace_back(
    //            static_cast<float>(r * std::sin(a1)),
    //            static_cast<float>(r * std::cos(a1)),
    //            static_cast<float>(z));
    //    }
    //}

    //// Azimuth meridians.
    //for (int azDeg = 0; azDeg < 360; azDeg += 15) {
    //    const double az = azDeg * kPi / 180.0;
    //    for (int i = 0; i < segments / 2; ++i) {
    //        const double t0 = -kPi / 2.0 + kPi * i / (segments / 2);
    //        const double t1 = -kPi / 2.0 + kPi * (i + 1) / (segments / 2);
    //        for (double t : {t0, t1}) {
    //            const double h = std::cos(t) * 1.006;
    //            m_wireVertices.emplace_back(
    //                static_cast<float>(h * std::sin(az)),
    //                static_cast<float>(h * std::cos(az)),
    //                static_cast<float>(std::sin(t) * 1.006));
    //        }
    //    }
    //}
}

void OpenGLSceneWidget::uploadPanoramaTexture()
{
    if (!m_glReady || m_panoramaTextureImage.isNull())
        return;

    // OpenGL texture origin is bottom-left; QImage source rows are top-down.//.mirrored(false, true)
    const QImage upload = m_panoramaTextureImage.convertToFormat(QImage::Format_RGBA8888);

    m_panoramaTexture.reset();
    m_panoramaTexture = std::make_unique<QOpenGLTexture>(QOpenGLTexture::Target2D);
    m_panoramaTexture->create();
    m_panoramaTexture->bind();
    m_panoramaTexture->setSize(upload.width(), upload.height());
    m_panoramaTexture->setFormat(QOpenGLTexture::RGBA8_UNorm);
    m_panoramaTexture->allocateStorage(QOpenGLTexture::RGBA, QOpenGLTexture::UInt8);
    m_panoramaTexture->setData(QOpenGLTexture::RGBA, QOpenGLTexture::UInt8, upload.constBits());
    m_panoramaTexture->setWrapMode(QOpenGLTexture::Repeat);
    m_panoramaTexture->setMinificationFilter(QOpenGLTexture::Linear);
    m_panoramaTexture->setMagnificationFilter(QOpenGLTexture::Linear);
    m_panoramaTexture->release();

    m_textureDirty = false;
}

void OpenGLSceneWidget::updateSceneGeometry()
{
    m_rayContextValid = CameraTransform::buildRayContext(
        m_cx, m_cy, m_cz,
        m_yaw, m_pitch, m_roll,
        m_hfov, m_vfov,
        m_outW, m_outH,
        m_flipVertical,
        m_rayCtx);

    m_hitValid = m_rayContextValid;
    if (!m_hitValid) return;

    const std::pair<double, double> corners[4] = {
        {0.0, 0.0},
        {m_outW - 1.0, 0.0},
        {0.0, m_outH - 1.0},
        {m_outW - 1.0, m_outH - 1.0}
    };

    for (int i = 0; i < 4; ++i) {
        double dir[3];
        if (!CameraTransform::pixelToENUDirection(
                m_rayCtx, corners[i].first, corners[i].second, dir) ||
            !raySphereIntersectionPoint(m_rayCtx.originENU, dir, m_hitPoints[i])) {
            m_hitValid = false;
            break;
        }
    }
}

void OpenGLSceneWidget::updatePerspective()
{
    if (m_panorama.width <= 0 || m_panorama.height <= 0)
        return;

    const HDRImage hdr = PanoramaProcessor::perspectiveFromPanorama(
        m_panorama,
        m_cx, m_cy, m_cz,
        m_yaw, m_pitch, m_roll,
        m_hfov, m_vfov,
        m_outW, m_outH,
        2,
		m_panoramaBasis,
        //m_northPanoramaDeg,
        m_flipVertical);

    const Image display = PanoramaProcessor::toneMapForDisplay(hdr, 1.0f, 2.2f);
    emit perspectiveViewReady(toQImage(display));
}

void OpenGLSceneWidget::drawTexturedSphere(const QMatrix4x4& mvp)
{
    if (!m_textureProgram || !m_panoramaTexture || m_sphereIndexCount <= 0)
        return;

    m_textureProgram->bind();
    m_textureProgram->setUniformValue("uMVP", mvp);
    m_textureProgram->setUniformValue("uOpacity", 0.65f);
    m_textureProgram->setUniformValue("uPanorama", 0);

    glActiveTexture(GL_TEXTURE0);
    m_panoramaTexture->bind();
    m_sphereVao.bind();
    m_sphereIbo.bind();
    glDrawElements(GL_TRIANGLES, m_sphereIndexCount, GL_UNSIGNED_INT, nullptr);
    m_sphereIbo.release();
    m_sphereVao.release();
    m_panoramaTexture->release();
    m_textureProgram->release();
}

void OpenGLSceneWidget::drawWireSphere(const QMatrix4x4& mvp)
{
    drawLines(m_wireVertices, QVector4D(0.82f, 0.84f, 0.88f, 0.33f), mvp, GL_LINES, 1.0f);
}

void OpenGLSceneWidget::drawWorldAxes(const QMatrix4x4& mvp)
{
    constexpr float L = 1.35f;
    drawLines({QVector3D(0,0,0), QVector3D(L,0,0)}, QVector4D(1.0f,0.25f,0.20f,1.0f), mvp, GL_LINES, 2.0f);
    drawLines({QVector3D(0,0,0), QVector3D(0,L,0)}, QVector4D(0.20f,0.95f,0.35f,1.0f), mvp, GL_LINES, 2.0f);
    drawLines({QVector3D(0,0,0), QVector3D(0,0,L)}, QVector4D(0.25f,0.55f,1.0f,1.0f), mvp, GL_LINES, 2.0f);
}

void OpenGLSceneWidget::drawCameraAxes(const QMatrix4x4& mvp)
{
    if (!m_rayContextValid) return;

    const QVector3D o(
        static_cast<float>(m_rayCtx.originENU[0]),
        static_cast<float>(m_rayCtx.originENU[1]),
        static_cast<float>(m_rayCtx.originENU[2]));

    double x[3], y[3], z[3];
    CameraTransform::getCameraAxesENU(m_rayCtx, x, y, z);
    constexpr float L = 0.28f;

    const QVector3D ex(static_cast<float>(x[0]), static_cast<float>(x[1]), static_cast<float>(x[2]));
    const QVector3D ey(static_cast<float>(y[0]), static_cast<float>(y[1]), static_cast<float>(y[2]));
    const QVector3D ez(static_cast<float>(z[0]), static_cast<float>(z[1]), static_cast<float>(z[2]));

    drawLines({o, o + L * ex}, QVector4D(1.0f,0.25f,0.20f,1.0f), mvp, GL_LINES, 3.0f);
    drawLines({o, o + L * ey}, QVector4D(0.20f,0.95f,0.35f,1.0f), mvp, GL_LINES, 3.0f);
    drawLines({o, o + L * ez}, QVector4D(1.0f,0.78f,0.15f,1.0f), mvp, GL_LINES, 3.0f);
}

void OpenGLSceneWidget::drawFrustumAndROI(const QMatrix4x4& mvp)
{
    if (!m_rayContextValid || !m_hitValid) return;

    const QVector3D o(
        static_cast<float>(m_rayCtx.originENU[0]),
        static_cast<float>(m_rayCtx.originENU[1]),
        static_cast<float>(m_rayCtx.originENU[2]));

    std::vector<QVector3D> rays;
    rays.reserve(8);
    for (const QVector3D& p : m_hitPoints) {
        rays.push_back(o);
        rays.push_back(p);
    }
    drawLines(rays, QVector4D(0.25f,1.0f,0.35f,0.95f), mvp, GL_LINES, 2.0f);

    const int order[5] = {0, 1, 3, 2, 0};
    std::vector<QVector3D> roi;
    roi.reserve(5);
    for (int idx : order)
        roi.push_back(m_hitPoints[idx] * 1.003f);
    drawLines(roi, QVector4D(1.0f,0.78f,0.12f,1.0f), mvp, GL_LINE_STRIP, 3.0f);
}

void OpenGLSceneWidget::drawCameraMarker(const QMatrix4x4& mvp)
{
    if (!m_rayContextValid) return;
    drawPoints({QVector3D(
        static_cast<float>(m_rayCtx.originENU[0]),
        static_cast<float>(m_rayCtx.originENU[1]),
        static_cast<float>(m_rayCtx.originENU[2]))},
        QVector4D(1.0f,0.25f,0.25f,1.0f), mvp, 10.0f);
}

void OpenGLSceneWidget::drawLines(
    const std::vector<QVector3D>& vertices,
    const QVector4D& color,
    const QMatrix4x4& mvp,
    GLenum primitive,
    float lineWidth)
{
    if (!m_colorProgram || vertices.empty()) return;

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

void OpenGLSceneWidget::drawPoints(
    const std::vector<QVector3D>& vertices,
    const QVector4D& color,
    const QMatrix4x4& mvp,
    float pointSize)
{
    if (!m_colorProgram || vertices.empty()) return;

    m_lineVao.bind();
    m_lineVbo.bind();
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

QPointF OpenGLSceneWidget::projectToWidget(const QVector3D& p, const QMatrix4x4& mvp) const
{
    const QVector4D clip = mvp * QVector4D(p, 1.0f);
    if (std::abs(clip.w()) < 1e-6f) return {};
    const QVector3D ndc = clip.toVector3DAffine();
    return QPointF(
        (ndc.x() * 0.5 + 0.5) * width(),
        (1.0 - (ndc.y() * 0.5 + 0.5)) * height());
}

void OpenGLSceneWidget::paintAxisLabels(const QMatrix4x4& mvp)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    auto label = [&](const QVector3D& pos, const QString& text, const QColor& color) {
        const QPointF s = projectToWidget(pos, mvp);
        p.setPen(color);
        p.drawText(s + QPointF(4, -4), text);
    };

    label(QVector3D(1.35f,0,0), "E", QColor(255,90,70));
    label(QVector3D(0,1.35f,0), "N", QColor(80,240,100));
    label(QVector3D(0,0,1.35f), "U", QColor(80,150,255));

	// 在这里加入：
	const QVector3D pe = panoramaEast(m_panoramaBasis);
	const QVector3D pn = panoramaNorth(m_panoramaBasis);
	const QVector3D pu = panoramaUp(m_panoramaBasis);
	constexpr float PL = 1.50f;
	label(pe * PL, "pE",
		QColor(255, 125, 55));
	label(pn * PL, "pN",
		QColor(70, 255, 165));
	label(pu * PL, "pU",
		QColor(210, 120, 255));


    if (m_rayContextValid) {
        const QVector3D o(
            static_cast<float>(m_rayCtx.originENU[0]),
            static_cast<float>(m_rayCtx.originENU[1]),
            static_cast<float>(m_rayCtx.originENU[2]));
        double x[3], y[3], z[3];
        CameraTransform::getCameraAxesENU(m_rayCtx, x, y, z);
        constexpr float L = 0.28f;
        label(o + L * QVector3D(x[0],x[1],x[2]), "Xc", QColor(255,90,70));
        label(o + L * QVector3D(y[0],y[1],y[2]), "Yc", QColor(80,240,100));
        label(o + L * QVector3D(z[0],z[1],z[2]), "Zc / Forward", QColor(255,205,50));
    }

    //p.setPen(QColor(220,220,225));
    //p.drawText(10, 20, "ENU world: X=East, Y=North, Z=Up");
    //p.drawText(10, 40, "Mouse: left-drag orbit, wheel zoom");
	p.setPen(QColor(220, 220, 225));

	p.drawText(
		10,
		20,
		"World ENU: E=(1,0,0) N=(0,1,0) U=(0,0,1)");

	p.drawText(
		10,
		40,
		QString(
			"Panorama N=(%1,%2,%3)")
		.arg(m_panoramaBasis.north[0], 0, 'f', 3)
		.arg(m_panoramaBasis.north[1], 0, 'f', 3)
		.arg(m_panoramaBasis.north[2], 0, 'f', 3));

	p.drawText(
		10,
		60,
		QString(
			"Panorama U=(%1,%2,%3)")
		.arg(m_panoramaBasis.up[0], 0, 'f', 3)
		.arg(m_panoramaBasis.up[1], 0, 'f', 3)
		.arg(m_panoramaBasis.up[2], 0, 'f', 3));

	p.drawText(
		10,
		80,
		"Mouse: left-drag orbit, wheel zoom");
}

bool OpenGLSceneWidget::raySphereIntersectionPoint(
    const double origin[3],
    const double dir[3],
    QVector3D& hit)
{
    const double a = dir[0]*dir[0] + dir[1]*dir[1] + dir[2]*dir[2];
    const double b = 2.0 * (origin[0]*dir[0] + origin[1]*dir[1] + origin[2]*dir[2]);
    const double c = origin[0]*origin[0] + origin[1]*origin[1] + origin[2]*origin[2] - 1.0;
    const double disc = b*b - 4.0*a*c;
    if (disc < 0.0) return false;

    const double root = std::sqrt(disc);
    const double t1 = (-b - root) / (2.0*a);
    const double t2 = (-b + root) / (2.0*a);
    const double t = t1 > 1e-6 ? t1 : (t2 > 1e-6 ? t2 : -1.0);
    if (t <= 1e-6) return false;

    hit = QVector3D(
        static_cast<float>(origin[0] + t * dir[0]),
        static_cast<float>(origin[1] + t * dir[1]),
        static_cast<float>(origin[2] + t * dir[2]));
    return true;
}

PanoramaLabel::PanoramaLabel(QWidget* parent)
    : QLabel(parent)
{
    setAlignment(Qt::AlignCenter);
    setMinimumSize(400, 200);
    setStyleSheet("border: 1px solid gray; background-color: #2a2a2a;");
}

void PanoramaLabel::setPanoramaImage(const Image& img)
{
    m_pixmap = QPixmap::fromImage(toQImage(img));
    update();
}

void PanoramaLabel::setCorners(const std::vector<QPointF>& corners)
{
    if (corners.size() >= 4) {
        m_corners = corners;
        m_hasCorners = true;
    } else {
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
    m_northDirectionDeg = wrap360(degrees);
    update();
}

void PanoramaLabel::paintEvent(QPaintEvent* event)
{
    if (m_pixmap.isNull()) {
        QLabel::paintEvent(event);
        return;
    }

    QPainter painter(this);
    const QPixmap scaled = m_pixmap.scaled(size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    const int x = (width() - scaled.width()) / 2;
    const int y = (height() - scaled.height()) / 2;
    painter.drawPixmap(x, y, scaled);

    const double imgW = scaled.width();
    const double imgH = scaled.height();
    const char* labels[4] = {"N", "E", "S", "W"};

    painter.setPen(QPen(QColor(255, 220, 40, 220), 1.5, Qt::DashLine));
    QFont f = painter.font();
    f.setBold(true);
    painter.setFont(f);

    for (int i = 0; i < 4; ++i) {
        const double deg = wrap360(m_northDirectionDeg + i * 90.0);
        const double u = deg / 360.0;
        const double px = x + u * imgW;
        painter.drawLine(QPointF(px, y), QPointF(px, y + imgH));
        painter.drawText(QPointF(px + 4.0, y + 18.0), labels[i]);
    }

    if (m_hasCorners && m_corners.size() >= 3) {
        QPolygonF poly;
        for (const auto& uv : m_corners)
            poly << QPointF(uv.x() * imgW + x, uv.y() * imgH + y);

        painter.setPen(QPen(Qt::red, 2));
        painter.drawPolyline(poly);
        if (poly.size() > 2)
            painter.drawLine(poly.last(), poly.first());
    }
}

void OpenGLSceneWidget::setPanoramaBasis(
	const PanoramaBasis& basis)
{
	bool changed = false;

	for (int i = 0; i < 3; ++i)
	{
		if (std::abs(
			m_panoramaBasis.east[i] -
			basis.east[i]) > 1e-9 ||
			std::abs(
				m_panoramaBasis.north[i] -
				basis.north[i]) > 1e-9 ||
			std::abs(
				m_panoramaBasis.up[i] -
				basis.up[i]) > 1e-9)
		{
			changed = true;
			break;
		}
	}

	if (!changed)
		return;

	m_panoramaBasis = basis;

	// UV 不变，但是球上的 UV -> world position
	// 关系发生变化。
	m_sphereDirty = true;
	// 经纬线也必须根据 Panorama ENU 更新
	m_wireDirty = true;
	// 灰色经纬网属于 Panorama 球自身，
	// 所以也必须跟随 PanoramaBasis。
	rebuildWireSphere();

	// Perspective 也必须使用新的 basis 重新采样。
	updatePerspective();

	update();
}

void OpenGLSceneWidget::drawPanoramaAxes(
	const QMatrix4x4& mvp)
{
	const QVector3D origin(0, 0, 0);

	const QVector3D e =
		panoramaEast(m_panoramaBasis);

	const QVector3D n =
		panoramaNorth(m_panoramaBasis);

	const QVector3D u =
		panoramaUp(m_panoramaBasis);

	constexpr float L = 1.48f;

	// Panorama East
	drawLines(
		{ origin, e * L },
		QVector4D(
			1.0f, 0.45f, 0.20f, 1.0f),
		mvp,
		GL_LINES,
		4.0f);

	// Panorama North
	drawLines(
		{ origin, n * L },
		QVector4D(
			0.25f, 1.0f, 0.65f, 1.0f),
		mvp,
		GL_LINES,
		4.0f);

	// Panorama Up
	drawLines(
		{ origin, u * L },
		QVector4D(
			0.80f, 0.45f, 1.0f, 1.0f),
		mvp,
		GL_LINES,
		4.0f);
}