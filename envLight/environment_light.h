#ifndef ENVIRONMENT_LIGHT_H
#define ENVIRONMENT_LIGHT_H

#include <vector>
#include <cmath>
#include <algorithm>
#include <memory>
#include "panorama_processor.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#ifndef M_PI_2
#define M_PI_2 (M_PI / 2.0)
#endif

// 方向
struct Direction {
    double theta;  // 天顶角 [0, π]
    double phi;    // 方位角 [0, 2π)
    Direction(double t = 0.0, double p = 0.0) : theta(t), phi(p) {}
};

struct EnvironmentSample {
    double radiance;  // 辐射度 (W/m²/sr)
    sRGB color;       // sRGB 颜色 (归一化)
};

// ================================================================
// 环境光模型抽象基类
// ================================================================
class EnvironmentLight {
public:
    virtual ~EnvironmentLight() = default;
    virtual double getRadiance(const Direction& dir) const = 0;
    virtual sRGB getRGB(const Direction& dir) const = 0;
    EnvironmentSample sample(const Direction& dir) const {
        return { getRadiance(dir), getRGB(dir) };
    }
};

// ================================================================
// 1. 均匀天空
// ================================================================
class UniformSky : public EnvironmentLight {
public:
    UniformSky(double radiance = 1.0, const sRGB& color = sRGB(1.0, 1.0, 1.0))
        : m_radiance(radiance), m_color(color) {}
    double getRadiance(const Direction&) const override { return m_radiance; }
    sRGB getRGB(const Direction&) const override { return m_color; }
private:
    double m_radiance;
    sRGB m_color;
};

// ================================================================
// 2. CIE 标准全阴天
// ================================================================
class CIEOvercastSky : public EnvironmentLight {
public:
    CIEOvercastSky(double zenithRadiance = 100.0,
        const sRGB& zenithColor = sRGB(1.0, 1.0, 0.9))
        : m_Lz(zenithRadiance), m_colorZenith(zenithColor) {}
    double getRadiance(const Direction& dir) const override {
        double cosTheta = std::cos(dir.theta);
        return m_Lz * (1.0 + 2.0 * cosTheta) / 3.0;
    }
    sRGB getRGB(const Direction& dir) const override {
        double cosTheta = std::cos(dir.theta);
        double weight = 0.6 + 0.4 * cosTheta;
        double warm = 1.0 - 0.3 * (1.0 - cosTheta);
        return sRGB(
            m_colorZenith.r * weight * warm,
            m_colorZenith.g * weight * (0.95 + 0.05 * cosTheta),
            m_colorZenith.b * weight * (0.85 + 0.15 * cosTheta)
        );
    }
private:
    double m_Lz;
    sRGB m_colorZenith;
};

// ================================================================
// 3. CIE 标准通用天空 (基于 Perez 模型，15 种类型)
// ================================================================
enum class CieSkyType {
    OvercastSteepGradation = 1,
    OvercastModerateGradation = 2,
    OvercastGradationSlightBrightening = 3,
    OvercastGradationBrightening = 4,
    OvercastBrightening = 5,
    PartlyCloudyNoBrightening = 6,
    PartlyCloudySlightBrightening = 7,
    PartlyCloudyModerateBrightening = 8,
    PartlyCloudyBright = 9,
    PartlyCloudyVeryBright = 10,
    ClearBlueSky = 11,
    ClearStandard = 12,
    ClearTurbid = 13,
    ClearTurbidBright = 14,
    ClearWhiteSky = 15
};

class CIESkyModel : public EnvironmentLight {
public:
    CIESkyModel(CieSkyType skyType, double sunTheta, double sunPhi,
        double zenithRadiance = 100.0,
        const sRGB& zenithColor = sRGB(1.0, 1.0, 0.9),
        double warmIntensity = 1.0)
        : m_type(skyType), m_sunTheta(sunTheta), m_sunPhi(sunPhi),
        m_Lz(zenithRadiance), m_colorZenith(zenithColor),
        m_warmIntensity(warmIntensity) {
        setParameters(skyType);
    }

    double getRadiance(const Direction& dir) const override {
        double relLum = getRelativeLuminance(dir);
        return m_Lz * relLum;
    }

    sRGB getRGB(const Direction& dir) const override {
        double relLum = getRelativeLuminance(dir);
        if (relLum <= 0.0) return sRGB(0.0, 0.0, 0.0);

        // 计算太阳与观测方向的夹角
        double chi = std::acos(
            std::cos(dir.theta) * std::cos(m_sunTheta) +
            std::sin(dir.theta) * std::sin(m_sunTheta) *
            std::cos(dir.phi - m_sunPhi)
        );

        // 暖色因子：根据太阳高度角自动调节
        double sunElevation = M_PI_2 - m_sunTheta;
        double warmFactor = 1.0 + m_warmIntensity * 0.8 * std::exp(-sunElevation * 8.0);
        warmFactor = std::max(1.0, std::min(2.0, warmFactor));

        // 动态天顶颜色
        double rBase = m_colorZenith.r * (0.6 + 0.4 * warmFactor);
        double gBase = m_colorZenith.g * (0.7 + 0.3 * warmFactor);
        double bBase = m_colorZenith.b * (0.8 + 0.2 * (1.0 - warmFactor / 2.0));

        double sunGlow = std::exp(-chi * chi * 30.0);
        double glowWarm = warmFactor * (0.8 + 0.2 * sunGlow);
        double cosTheta = std::cos(dir.theta);
        double blueFactor = 0.6 + 0.4 * cosTheta;
        double redFactor = 0.6 + 0.4 * (1.0 - cosTheta);
        double normLum = std::min(relLum, 1.0);

        return sRGB(
            normLum * rBase * redFactor * glowWarm,
            normLum * gBase * (0.9 + 0.1 * cosTheta) * (0.8 + 0.2 * sunGlow),
            normLum * bBase * blueFactor * (0.8 + 0.2 * (1.0 - sunGlow))
        );
    }

    // 获取相对亮度（相对于天顶）
    double getRelativeLuminance(const Direction& dir) const {
        double chi = std::acos(
            std::cos(dir.theta) * std::cos(m_sunTheta) +
            std::sin(dir.theta) * std::sin(m_sunTheta) *
            std::cos(dir.phi - m_sunPhi)
        );
        auto gradation = [this](double theta) -> double {
            double cosTheta = std::cos(theta);
            if (cosTheta < 1e-6) cosTheta = 1e-6;
            return 1.0 + m_a * std::exp(m_b / cosTheta);
        };
        auto indicatrix = [this](double chi) -> double {
            return 1.0 + m_c * (std::exp(m_d * chi) - std::exp(m_d * M_PI_2))
                + m_e * std::cos(chi) * std::cos(chi);
        };
        double f_theta = gradation(dir.theta);
        double f_sun = gradation(m_sunTheta);
        double g_chi = indicatrix(chi);
        double g_zero = indicatrix(0.0);
        if (f_sun == 0.0 || g_zero == 0.0) return 0.0;
        return (f_theta * g_chi) / (f_sun * g_zero);
    }

    Direction getSunDirection() const { return Direction(m_sunTheta, m_sunPhi); }
    CieSkyType getSkyType() const { return m_type; }
    double getZenithLuminance() const { return m_Lz; }
    void setWarmIntensity(double intensity) { m_warmIntensity = intensity; }

private:
    void setParameters(CieSkyType type) {
        // 15 种天空类型的 (a, b, c, d, e) 参数
        static const double params[15][5] = {
            { 4.0, -0.70,  0.0, -1.0,  0.0 },
            { 4.0, -0.70,  2.0, -1.0,  0.0 },
            { 1.1, -0.80,  0.0, -1.0,  0.0 },
            { 1.1, -0.80,  2.0, -1.0,  0.0 },
            { 0.0, -1.00,  5.0, -1.0,  0.0 },
            {-1.0, -0.50,  0.0, -1.0,  0.0 },
            {-1.0, -0.50,  2.0, -1.0,  0.0 },
            {-1.0, -0.80,  5.0, -1.0,  0.0 },
            {-1.0, -0.80, 10.0, -1.0,  0.0 },
            {-1.0, -1.00, 10.0, -1.0,  0.0 },
            {-1.0, -0.50,  0.0, -1.0,  0.0 },
            {-1.0, -0.50,  2.0, -1.0,  0.0 },
            {-1.0, -0.50,  5.0, -1.0,  0.0 },
            {-1.0, -0.50, 10.0, -1.0,  0.0 },
            {-1.0, -0.70, 15.0, -1.0,  0.0 }
        };
        int idx = static_cast<int>(type) - 1;
        idx = std::max(0, std::min(idx, 14));
        m_a = params[idx][0];
        m_b = params[idx][1];
        m_c = params[idx][2];
        m_d = params[idx][3];
        m_e = params[idx][4];
    }

    CieSkyType m_type;
    double m_sunTheta, m_sunPhi;
    double m_Lz;
    sRGB m_colorZenith;
    double m_warmIntensity;
    double m_a, m_b, m_c, m_d, m_e;
};

// 为兼容原有命名，保留 CIEGeneralSky 作为别名
typedef CIESkyModel CIEGeneralSky;

// ================================================================
// 4. U.S. Standard Atmosphere 1976 (简化)
// ================================================================
class USStandardAtmosphere1976Light : public EnvironmentLight {
public:
    USStandardAtmosphere1976Light(double sunZenith, double altitude_km = 0.0,
        double sunConst = 1.0)
        : m_sunZenith(sunZenith), m_alt(altitude_km), m_sunConst(sunConst) {}

    double getRadiance(const Direction& dir) const override {
        double cosZenith = std::cos(dir.theta);
        double rayleigh = (1.0 + cosZenith * cosZenith) / 2.0;
        double angularDist = std::acos(
            std::sin(m_sunZenith) * std::sin(dir.theta) * std::cos(m_sunZenith - dir.phi) +
            std::cos(m_sunZenith) * std::cos(dir.theta)
        );
        double sunDisc = (angularDist < 0.05) ? 50.0 * m_sunConst : 0.0;
        return rayleigh + sunDisc;
    }

    sRGB getRGB(const Direction& dir) const override {
        double rad = getRadiance(dir);
        if (rad <= 0.0) return sRGB(0.0, 0.0, 0.0);
        double angularDist = std::acos(
            std::sin(m_sunZenith) * std::sin(dir.theta) * std::cos(m_sunZenith - dir.phi) +
            std::cos(m_sunZenith) * std::cos(dir.theta)
        );
        bool isSun = (angularDist < 0.05);
        if (isSun) {
            double trans = std::exp(-0.008735 * std::pow(550.0 / 500.0, 4.0) / std::cos(m_sunZenith));
            return sRGB(rad * trans, rad * trans, rad * trans * 1.1);
        }
        else {
            double blueFactor = 0.8 + 0.2 * std::cos(dir.theta);
            return sRGB(rad * 0.6, rad * 0.8, rad * blueFactor);
        }
    }

private:
    double m_sunZenith, m_alt, m_sunConst;
};

#endif // ENVIRONMENT_LIGHT_H