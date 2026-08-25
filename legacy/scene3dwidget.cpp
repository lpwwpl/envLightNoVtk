#include "scene3dwidget.h"
#include <cmath>
#include <GL/glu.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

Scene3DWidget::Scene3DWidget(QWidget* parent)
    : QOpenGLWidget(parent)
    , m_cx(0), m_cy(0), m_cz(0)
    , m_yaw(0), m_pitch(0), m_roll(0)
    , m_hfov(90), m_vfov(-1)
    , m_outW(800), m_outH(600)
    , m_paramsValid(false)
    , m_cornersValid(false)
    , m_roiValid(false)
    , m_showROI(true)
{
    setMinimumSize(400, 400);
}

Scene3DWidget::~Scene3DWidget()
{
    makeCurrent();
    m_vbo.destroy();
    m_ibo.destroy();
    doneCurrent();
}

void Scene3DWidget::setCameraParams(double cx, double cy, double cz,
    double yaw, double pitch, double roll,
    double hfov, double vfov,
    int outW, int outH)
{
    m_cx = cx; m_cy = cy; m_cz = cz;
    m_yaw = yaw; m_pitch = pitch; m_roll = roll;
    m_hfov = hfov; m_vfov = vfov;
    m_outW = outW; m_outH = outH;
    m_paramsValid = true;

    // 计算四个角点在球面上的位置
    double yaw_rad = yaw * M_PI / 180.0;
    double pitch_rad = pitch * M_PI / 180.0;
    double roll_rad = roll * M_PI / 180.0;
    double cyaw = cos(yaw_rad), syaw = sin(yaw_rad);
    double cp = cos(pitch_rad), sp = sin(pitch_rad);
    double cr = cos(roll_rad), sr = sin(roll_rad);
    double R[3][3] = {
        { cyaw * cp,  cyaw * sp * sr - syaw * cr,  cyaw * sp * cr + syaw * sr },
        { syaw * cp,  syaw * sp * sr + cyaw * cr,  syaw * sp * cr - cyaw * sr },
        { -sp,      cp * sr,                  cp * cr }
    };

    double hfov_rad = hfov * M_PI / 180.0;
    double focalX = (outW / 2.0) / tan(hfov_rad / 2.0);
    double focalY;
    if (vfov > 0) {
        double vfov_rad = vfov * M_PI / 180.0;
        focalY = (outH / 2.0) / tan(vfov_rad / 2.0);
    }
    else {
        focalY = focalX * (static_cast<double>(outH) / outW);
    }
    double halfW = outW / 2.0;
    double halfH = outH / 2.0;

    double cameraPos[3] = { cx, cy, cz };
    double corners_x[4] = { 0.0, static_cast<double>(outW - 1), 0.0, static_cast<double>(outW - 1) };
    double corners_y[4] = { 0.0, 0.0, static_cast<double>(outH - 1), static_cast<double>(outH - 1) };

    m_cornersValid = true;
    for (int i = 0; i < 4; ++i) {
        double x = corners_x[i];
        double y = corners_y[i];
        double nx = (x - halfW) / focalX;
        double ny = (y - halfH) / focalY;
        double nz = 1.0;
        double len = sqrt(nx * nx + ny * ny + nz * nz);
        nx /= len; ny /= len; nz /= len;

        double wx = R[0][0] * nx + R[0][1] * ny + R[0][2] * nz;
        double wy = R[1][0] * nx + R[1][1] * ny + R[1][2] * nz;
        double wz = R[2][0] * nx + R[2][1] * ny + R[2][2] * nz;
        len = sqrt(wx * wx + wy * wy + wz * wz);
        wx /= len; wy /= len; wz /= len;

        double dir[3] = { wx, wy, wz };
        double a = dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2];
        double b = 2.0 * (cameraPos[0] * dir[0] + cameraPos[1] * dir[1] + cameraPos[2] * dir[2]);
        double c = cameraPos[0] * cameraPos[0] + cameraPos[1] * cameraPos[1] + cameraPos[2] * cameraPos[2] - 1.0;
        double disc = b * b - 4.0 * a * c;
        if (disc < 0) { m_cornersValid = false; break; }
        double t = (-b - sqrt(disc)) / (2.0 * a);
        if (t <= 0) t = (-b + sqrt(disc)) / (2.0 * a);
        if (t <= 0) { m_cornersValid = false; break; }
        double hit_x = cameraPos[0] + t * dir[0];
        double hit_y = cameraPos[1] + t * dir[1];
        double hit_z = cameraPos[2] + t * dir[2];
        m_corners[i] = QVector3D(hit_x, hit_y, hit_z);
    }
    update();
}

void Scene3DWidget::setROI(double u0, double u1, double v0, double v1)
{
    m_roi_u0 = u0; m_roi_u1 = u1; m_roi_v0 = v0; m_roi_v1 = v1;
    m_roiValid = true;
    update();
}

void Scene3DWidget::initializeGL()
{
    initializeOpenGLFunctions();
    glClearColor(0.1f, 0.1f, 0.2f, 1.0f);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LINE_SMOOTH);
    glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
    setupSphere();
}

void Scene3DWidget::resizeGL(int w, int h)
{
    glViewport(0, 0, w, h);
}

void Scene3DWidget::paintGL()
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(45.0, width() / (double)height(), 0.1, 10.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    gluLookAt(0, 0, 3, 0, 0, 0, 0, 1, 0);

    drawSphere();
    drawCamera();
    if (m_cornersValid) drawFrustum();
    if (m_roiValid && m_showROI) drawROIOnSphere();
}

void Scene3DWidget::setupSphere()
{
    const int latSteps = 18;
    const int lonSteps = 36;
    const float radius = 1.0f;

    for (int i = 0; i <= latSteps; ++i) {
        float lat = M_PI * i / latSteps;
        float y = radius * cos(lat);
        float r = radius * sin(lat);
        for (int j = 0; j <= lonSteps; ++j) {
            float lon = 2 * M_PI * j / lonSteps;
            float x = r * cos(lon);
            float z = r * sin(lon);
            m_sphereVertices.push_back(x);
            m_sphereVertices.push_back(y);
            m_sphereVertices.push_back(z);
        }
    }

    for (int i = 0; i < latSteps; ++i) {
        for (int j = 0; j < lonSteps; ++j) {
            int idx = i * (lonSteps + 1) + j;
            m_sphereIndices.push_back(idx);
            m_sphereIndices.push_back(idx + 1);
            m_sphereIndices.push_back(idx);
            m_sphereIndices.push_back(idx + (lonSteps + 1));
        }
    }

    m_vbo.create();
    m_vbo.bind();
    m_vbo.allocate(m_sphereVertices.data(), m_sphereVertices.size() * sizeof(float));
    m_ibo.create();
    m_ibo.bind();
    m_ibo.allocate(m_sphereIndices.data(), m_sphereIndices.size() * sizeof(unsigned int));
    m_vbo.release();
    m_ibo.release();
}

void Scene3DWidget::drawSphere()
{
    glColor3f(0.7f, 0.7f, 0.7f);
    glLineWidth(1.0f);
    m_vbo.bind();
    m_ibo.bind();
    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(3, GL_FLOAT, 0, 0);
    glDrawElements(GL_LINES, m_sphereIndices.size(), GL_UNSIGNED_INT, 0);
    glDisableClientState(GL_VERTEX_ARRAY);
    m_vbo.release();
    m_ibo.release();
}

void Scene3DWidget::drawWireCube(float size)
{
    float s = size * 0.5f;
    glBegin(GL_LINE_STRIP);
    glVertex3f(-s, -s, -s); glVertex3f(s, -s, -s);
    glVertex3f(s, -s, s); glVertex3f(-s, -s, s); glVertex3f(-s, -s, -s);
    glVertex3f(-s, s, -s); glVertex3f(s, s, -s); glVertex3f(s, s, s);
    glVertex3f(-s, s, s); glVertex3f(-s, s, -s);
    glEnd();
    glBegin(GL_LINES);
    glVertex3f(-s, -s, -s); glVertex3f(-s, s, -s);
    glVertex3f(s, -s, -s); glVertex3f(s, s, -s);
    glVertex3f(s, -s, s); glVertex3f(s, s, s);
    glVertex3f(-s, -s, s); glVertex3f(-s, s, s);
    glEnd();
}

void Scene3DWidget::drawCamera()
{
    QVector3D pos(m_cx, m_cy, m_cz);
    glPushMatrix();
    glTranslatef(pos.x(), pos.y(), pos.z());
    glColor3f(1.0f, 0.5f, 0.2f);
    drawWireCube(0.08f);
    glPopMatrix();

    double yaw_rad = m_yaw * M_PI / 180.0;
    double pitch_rad = m_pitch * M_PI / 180.0;
    double roll_rad = m_roll * M_PI / 180.0;
    double cyaw = cos(yaw_rad), syaw = sin(yaw_rad);
    double cp = cos(pitch_rad), sp = sin(pitch_rad);
    double cr = cos(roll_rad), sr = sin(roll_rad);
    double R[3][3] = {
        { cyaw * cp,  cyaw * sp * sr - syaw * cr,  cyaw * sp * cr + syaw * sr },
        { syaw * cp,  syaw * sp * sr + cyaw * cr,  syaw * sp * cr - cyaw * sr },
        { -sp,      cp * sr,                  cp * cr }
    };
    QVector3D forward(-R[0][2], -R[1][2], -R[2][2]);
    forward.normalize();
    QVector3D end = pos + forward * 0.4;
    glBegin(GL_LINES);
    glVertex3f(pos.x(), pos.y(), pos.z());
    glVertex3f(end.x(), end.y(), end.z());
    glEnd();
    glPushMatrix();
    glTranslatef(end.x(), end.y(), end.z());
    drawWireCube(0.05f);
    glPopMatrix();
}

void Scene3DWidget::drawFrustum()
{
    QVector3D pos(m_cx, m_cy, m_cz);
    glColor3f(0.2f, 1.0f, 0.2f);
    glLineWidth(2.0f);
    glBegin(GL_LINES);
    for (int i = 0; i < 4; ++i) {
        glVertex3f(pos.x(), pos.y(), pos.z());
        glVertex3f(m_corners[i].x(), m_corners[i].y(), m_corners[i].z());
    }
    glEnd();
    glBegin(GL_LINE_LOOP);
    for (int i = 0; i < 4; ++i) {
        glVertex3f(m_corners[i].x(), m_corners[i].y(), m_corners[i].z());
    }
    glEnd();
}

QVector3D Scene3DWidget::uvToWorld(double u, double v) const
{
    double theta = v * M_PI;
    double phi = (u * 2.0 * M_PI) - M_PI;
    double x = sin(theta) * cos(phi);
    double y = cos(theta);
    double z = sin(theta) * sin(phi);
    return QVector3D(x, y, z);
}

void Scene3DWidget::drawROIOnSphere()
{
    glColor3f(1.0f, 0.2f, 0.2f);
    glLineWidth(3.0f);
    auto drawRect = [this](double u0, double u1, double v0, double v1) {
        const int steps = 30;
        glBegin(GL_LINE_STRIP);
        for (int i = 0; i <= steps; ++i) {
            double u = u0 + (u1 - u0) * i / steps;
            QVector3D p = uvToWorld(u, v0);
            glVertex3f(p.x(), p.y(), p.z());
        }
        glEnd();
        glBegin(GL_LINE_STRIP);
        for (int i = 0; i <= steps; ++i) {
            double u = u0 + (u1 - u0) * i / steps;
            QVector3D p = uvToWorld(u, v1);
            glVertex3f(p.x(), p.y(), p.z());
        }
        glEnd();
        glBegin(GL_LINE_STRIP);
        for (int i = 0; i <= steps; ++i) {
            double v = v0 + (v1 - v0) * i / steps;
            QVector3D p = uvToWorld(u0, v);
            glVertex3f(p.x(), p.y(), p.z());
        }
        glEnd();
        glBegin(GL_LINE_STRIP);
        for (int i = 0; i <= steps; ++i) {
            double v = v0 + (v1 - v0) * i / steps;
            QVector3D p = uvToWorld(u1, v);
            glVertex3f(p.x(), p.y(), p.z());
        }
        glEnd();
    };

    if (m_roi_u0 > m_roi_u1) {
        drawRect(m_roi_u0, 1.0, m_roi_v0, m_roi_v1);
        drawRect(0.0, m_roi_u1, m_roi_v0, m_roi_v1);
    }
    else {
        drawRect(m_roi_u0, m_roi_u1, m_roi_v0, m_roi_v1);
    }
}