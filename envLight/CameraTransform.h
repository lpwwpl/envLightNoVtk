#pragma once

#include <cmath>

// CameraTransform
// ----------------
// Two coordinate systems intentionally coexist:
//
// Camera local (traditional camera coordinates):
//   +Xc : camera local X
//   +Yc : camera local Y
//   +Zc : forward / optical axis
//
// World / Panorama / VTK coordinates (ENU):
//   +X : East
//   +Y : North
//   +Z : Up
//
// UI yaw/pitch/roll always control the traditional camera coordinate system
// through the historical Euler matrix Rz(yaw)*Ry(pitch)*Rx(roll).
// At zero Euler rotation the fixed camera-basis-to-ENU mapping is:
//   +Xc -> +Y ENU (North)
//   +Yc -> +Z ENU (Up)
//   +Zc -> +X ENU (East / camera forward at zero pose)
//
// Therefore:
//   cameraToENU = M_basis * R_euler
//
// UI Camera X/Y/Z are LOCAL camera-axis translations.  Thus an input
// position (cx,cy,cz) is converted to ENU with the SAME cameraToENU matrix.
// Changing RPY changes the ENU direction in which Xc/Yc/Zc translation moves.

namespace CameraTransform {

constexpr double kPi = 3.14159265358979323846;

// Preserve the historical projection convention: image Y grows downward and
// camera-local +Y also grows downward.  If your physical camera convention is
// +Y upward, set this to false.
constexpr bool kCameraPositiveYIsImageDown = true;

struct RayContext {
    // Camera-local vector -> ENU world vector.
    double cameraToENU[3][3]{};

    // UI camera-local Xc/Yc/Zc translated into ENU world position.
    double originENU[3]{};

    double focalX = 1.0;
    double focalY = 1.0;
    double halfW = 0.0;
    double halfH = 0.0;
    int outW = 0;
    int outH = 0;
    bool flipVertical = false;
};

inline bool normalize3(double v[3]) {
    const double len = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (len < 1e-12) return false;
    v[0] /= len;
    v[1] /= len;
    v[2] /= len;
    return true;
}

inline void multiply3x3(const double A[3][3], const double B[3][3], double C[3][3]) {
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            C[r][c] = 0.0;
            for (int k = 0; k < 3; ++k)
                C[r][c] += A[r][k] * B[k][c];
        }
    }
}

inline void multiplyMatVec(const double R[3][3], const double v[3], double out[3]) {
    out[0] = R[0][0] * v[0] + R[0][1] * v[1] + R[0][2] * v[2];
    out[1] = R[1][0] * v[0] + R[1][1] * v[1] + R[1][2] * v[2];
    out[2] = R[2][0] * v[0] + R[2][1] * v[1] + R[2][2] * v[2];
}

// Historical UI Euler rotation.  This is deliberately NOT replaced by an ENU
// navigation yaw/elevation model.
inline void buildCameraEulerRotation(
    double yawDeg,
    double pitchDeg,
    double rollDeg,
    double R[3][3])
{
    const double yaw = yawDeg * kPi / 180.0;
    const double pitch = pitchDeg * kPi / 180.0;
    const double roll = rollDeg * kPi / 180.0;

    const double cyaw = std::cos(yaw);
    const double syaw = std::sin(yaw);
    const double cp = std::cos(pitch);
    const double sp = std::sin(pitch);
    const double cr = std::cos(roll);
    const double sr = std::sin(roll);

    R[0][0] = cyaw * cp;
    R[0][1] = cyaw * sp * sr - syaw * cr;
    R[0][2] = cyaw * sp * cr + syaw * sr;

    R[1][0] = syaw * cp;
    R[1][1] = syaw * sp * sr + cyaw * cr;
    R[1][2] = syaw * sp * cr - cyaw * sr;

    R[2][0] = -sp;
    R[2][1] = cp * sr;
    R[2][2] = cp * cr;
}

inline void buildCameraBasisToENU(double M[3][3]) {
    // Xenu = Zcamera
    // Yenu = Xcamera
    // Zenu = Ycamera
    M[0][0] = 0.0; M[0][1] = 0.0; M[0][2] = 1.0;
    M[1][0] = 1.0; M[1][1] = 0.0; M[1][2] = 0.0;
    M[2][0] = 0.0; M[2][1] = 1.0; M[2][2] = 0.0;
}

inline void buildCameraToENURotation(
    double yawDeg,
    double pitchDeg,
    double rollDeg,
    double outR[3][3])
{
    double eulerR[3][3];
    double basis[3][3];
    buildCameraEulerRotation(yawDeg, pitchDeg, rollDeg, eulerR);
    buildCameraBasisToENU(basis);

    // Apply the traditional camera Euler operation first, then express the
    // result in the fixed ENU / Panorama world basis.
    multiply3x3(basis, eulerR, outR);
}

inline void localPositionToENU(
    const double cameraToENU[3][3],
    double cameraX,
    double cameraY,
    double cameraZ,
    double originENU[3])
{
    const double local[3] = { cameraX, cameraY, cameraZ };
    multiplyMatVec(cameraToENU, local, originENU);
}

inline bool buildRayContext(
    double cameraX,
    double cameraY,
    double cameraZ,
    double yawDeg,
    double pitchDeg,
    double rollDeg,
    double hfovDeg,
    double vfovDeg,
    int outW,
    int outH,
    bool flipVertical,
    RayContext& ctx)
{
    if (outW <= 0 || outH <= 0) return false;
    if (!(hfovDeg > 0.0 && hfovDeg < 180.0)) return false;

    buildCameraToENURotation(yawDeg, pitchDeg, rollDeg, ctx.cameraToENU);
    localPositionToENU(ctx.cameraToENU, cameraX, cameraY, cameraZ, ctx.originENU);

    const double hfov = hfovDeg * kPi / 180.0;
    ctx.focalX = (outW / 2.0) / std::tan(hfov / 2.0);

    if (vfovDeg > 0.0) {
        const double vfov = vfovDeg * kPi / 180.0;
        ctx.focalY = (outH / 2.0) / std::tan(vfov / 2.0);
    }
    else {
        ctx.focalY = ctx.focalX *
            (static_cast<double>(outH) / static_cast<double>(outW));
    }

    ctx.halfW = outW / 2.0;
    ctx.halfH = outH / 2.0;
    ctx.outW = outW;
    ctx.outH = outH;
    ctx.flipVertical = flipVertical;
    return true;
}

inline bool pixelToCameraDirection(
    const RayContext& ctx,
    double pixelX,
    double pixelY,
    double dirCamera[3])
{
    dirCamera[0] = (pixelX - ctx.halfW) / ctx.focalX;
    const double imageY = (pixelY - ctx.halfH) / ctx.focalY;
    const double baseCameraY = kCameraPositiveYIsImageDown ? imageY : -imageY;
    dirCamera[1] = ctx.flipVertical ? -baseCameraY : baseCameraY;
    dirCamera[2] = 1.0; // traditional camera forward
    return normalize3(dirCamera);
}

inline bool cameraDirectionToENU(
    const RayContext& ctx,
    const double dirCamera[3],
    double dirENU[3])
{
    multiplyMatVec(ctx.cameraToENU, dirCamera, dirENU);
    return normalize3(dirENU);
}

inline bool pixelToENUDirection(
    const RayContext& ctx,
    double pixelX,
    double pixelY,
    double dirENU[3])
{
    double dirCamera[3];
    if (!pixelToCameraDirection(ctx, pixelX, pixelY, dirCamera)) return false;
    return cameraDirectionToENU(ctx, dirCamera, dirENU);
}

inline void getCameraAxesENU(
    const RayContext& ctx,
    double xAxisENU[3],
    double yAxisENU[3],
    double zAxisENU[3])
{
    // Matrix columns are Camera Xc/Yc/Zc axes represented in ENU world.
    for (int r = 0; r < 3; ++r) {
        xAxisENU[r] = ctx.cameraToENU[r][0];
        yAxisENU[r] = ctx.cameraToENU[r][1];
        zAxisENU[r] = ctx.cameraToENU[r][2];
    }
}

} // namespace CameraTransform
