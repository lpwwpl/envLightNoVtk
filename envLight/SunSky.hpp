#ifndef SUNSKY_HPP
#define SUNSKY_HPP

#include "VL234f.hpp"

namespace SSLib {

    // CIE 天空模型系数
    struct CIESkyCoefficients {
        float a = 0.0f;
        float b = 0.0f;
        float c = 0.0f;
        float d = 0.0f;
        float e = 0.0f;
    };

    // 计算太阳方向（+X 东，+Y 北，+Z 天顶）
    // 参数：decimalHour (当地太阳时，如 10.5)，timeZone (UTC 偏移)，dayOfYear (1-366)，latitude/longitude (度)
    Vec3f SunDirection(float decimalHour, float timeZone, int dayOfYear,
        float latitudeDeg, float longitudeDeg);

    // CIE 标准天空相对亮度（相对于天顶亮度 zenithValue）
    float CIEStandardSky(int type, const Vec3f& direction, const Vec3f& toSun, float zenithValue);

    // 获取标准天空的系数
    CIESkyCoefficients CIEStandardSkyCoefficients(int type);

    // 自定义系数天空
    float CIECustomSky(const CIESkyCoefficients& coeff, const Vec3f& direction,
        const Vec3f& toSun, float zenithValue);

} // namespace SSLib

#endif // SUNSKY_HPP