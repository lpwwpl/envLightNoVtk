#include "SunSky.hpp"

#include <algorithm>
#include <cmath>


namespace SSLib {

namespace {

constexpr float PI =
    3.14159265358979323846f;
constexpr float HALF_PI = PI * 0.5f;
constexpr float DEG2RAD = PI / 180.0f;


// CIE S 011/E:2003 / ISO 15469:2004.
// Rows are CIE sky types 1-15; API remains 0-based.
const float kCoefficients[15][5] = {
    // a,     b,      c,     d,      e
    { 4.0f,  -0.70f,  0.0f, -1.0f,  0.00f }, // 1
    { 4.0f,  -0.70f,  2.0f, -1.5f,  0.15f }, // 2
    { 1.1f,  -0.80f,  0.0f, -1.0f,  0.00f }, // 3
    { 1.1f,  -0.80f,  2.0f, -1.5f,  0.15f }, // 4
    { 0.0f,  -1.00f,  0.0f, -1.0f,  0.00f }, // 5
    { 0.0f,  -1.00f,  2.0f, -1.5f,  0.15f }, // 6
    { 0.0f,  -1.00f,  5.0f, -2.5f,  0.30f }, // 7
    { 0.0f,  -1.00f, 10.0f, -3.0f,  0.45f }, // 8
    {-1.0f,  -0.55f,  2.0f, -1.5f,  0.15f }, // 9
    {-1.0f,  -0.55f,  5.0f, -2.5f,  0.30f }, // 10
    {-1.0f,  -0.55f, 10.0f, -3.0f,  0.45f }, // 11
    {-1.0f,  -0.32f, 10.0f, -3.0f,  0.45f }, // 12
    {-1.0f,  -0.32f, 16.0f, -3.0f,  0.30f }, // 13
    {-1.0f,  -0.15f, 16.0f, -3.0f,  0.30f }, // 14
    {-1.0f,  -0.15f, 24.0f, -2.8f,  0.15f }  // 15
};

//clampFloat(15.0f, 0.0f, 10.0f);  // 返回 10.0f
//clampFloat(-3.0f, 0.0f, 10.0f);  // 返回 0.0f
//clampFloat(5.0f, 0.0f, 10.0f);  // 返回 5.0f
float clampFloat(float value, float low, float high)
{
	return std::max(low, std::min(value, high));
}

float vectorLength(const Vec3f& value)
{
    return std::sqrt(
        value.v[0] * value.v[0]
        + value.v[1] * value.v[1]
        + value.v[2] * value.v[2]);
}

Vec3f normalized(const Vec3f& value)
{
    const float length = vectorLength(value);

    if (length <= 1.0e-8f)
        return {0.0f, 0.0f, 1.0f};

    return {
        value.v[0] / length,
        value.v[1] / length,
        value.v[2] / length
    };
}
//基于天顶角的亮度渐变函数,常见于大气散射 / 天空渲染（比如 Preetham、Hosek、Bruneton 天空模型
//它根据视线与天顶的夹角，返回一个亮度缩放系数，让天空（或太阳）的亮度从天顶到地平线有一个平滑的指数过渡。
//视线与天顶方向的夹角（弧度）。0 = 正上方天顶，π/2 = 地平线
//振幅系数a，控制渐变强度（决定亮多少 / 暗多少）
//指数系数b，控制渐变“收窄”的快慢，一般取负值
float luminanceGradation(float zenithAngle, float a, float b)
{
	if (zenithAngle >= HALF_PI)
		return 1.0f;

	const float cosZenith = std::max(1.0e-5f, std::cos(zenithAngle));
	//Perez
	return 1.0f + a * std::exp(b / cosZenith);
}

//CIE 标准通用天空模型（ISO 15469 / CIE S 011） 以及 Perez 全天空模型 中的散射指示函数（Scattering Indicatrix），与前面那个 luminanceGradation 是配套使用的
//它描述的是：天空亮度随着“观察方向与太阳方向之间的夹角（散射角）”如何变化——也就是太阳周围的光晕、以及天空散射造成的亮度分布。
//luminanceGradation 负责垂直方向的亮度变化（天顶 → 地平线）。
//scatteringIndicatrix 负责水平 / 角度方向的亮度变化（靠近太阳 → 远离太阳）。
float scatteringIndicatrix(float angularDistance, float c, float d, float e)
{
	const float cosine = std::cos(angularDistance);

	// Standard form:
	// 1 + c[exp(d*chi) - exp(d*pi/2)] + e*cos^2(chi)
	return 1.0f + c * (std::exp(d * angularDistance) - std::exp(d * HALF_PI)) + e * cosine * cosine;
}

//给定一个天空方向和一个太阳方向，返回该方向上的相对亮度（相对于天顶亮度归一化）。
float relativeLuminance(const Vec3f& rawDirection, const Vec3f& rawSunDirection, float a, float b, float c, float d, float e)
{
	const Vec3f direction = normalized(rawDirection);
	const Vec3f sunDirection = normalized(rawSunDirection);

	if (direction.v[2] <= 0.0f)
		return 0.0f;

	const float zenithAngle = std::acos(clampFloat(direction.v[2], -1.0f, 1.0f));

	const float sunZenithAngle = std::acos(clampFloat(sunDirection.v[2], -1.0f, 1.0f));

	const float cosineDistance = clampFloat(direction.dot(sunDirection), -1.0f, 1.0f);

	const float angularDistance = std::acos(cosineDistance);

	const float numerator = luminanceGradation(zenithAngle, a, b)        * scatteringIndicatrix(angularDistance, c, d, e);

	const float denominator = luminanceGradation(0.0f, a, b)        * scatteringIndicatrix(sunZenithAngle, c, d, e);

	if (std::abs(denominator) <= 1.0e-8f)
		return 0.0f;

	return std::max(0.0f, numerator / denominator);
}

} // namespace

//根据日期、时间、经纬度计算太阳方向 的函数，用的是 NOAA（美国国家海洋和大气管理局）太阳位置近似算法。它返回一个从观察者指向太阳的单位向量，坐标系是 东-北-上（East-North-Up，ENU）
Vec3f SunDirection(float decimalHour, float timeZone, int dayOfYear, float latitudeDeg, float longitudeDeg)
{
	const float latitude = latitudeDeg * DEG2RAD;

	// Fractional year and NOAA-style engineering approximation.
	const float gamma = 2.0f * PI / 365.0f * (static_cast<float>(dayOfYear - 1) + (decimalHour - 12.0f) / 24.0f);

	const float equationOfTime = 229.18f * (0.000075f + 0.001868f * std::cos(gamma) - 0.032077f * std::sin(gamma) - 0.014615f * std::cos(2.0f * gamma) - 0.040849f * std::sin(2.0f * gamma));

	const float declination =
		0.006918f
		- 0.399912f * std::cos(gamma)
		+ 0.070257f * std::sin(gamma)
		- 0.006758f * std::cos(2.0f * gamma)
		+ 0.000907f * std::sin(2.0f * gamma)
		- 0.002697f * std::cos(3.0f * gamma)
		+ 0.001480f * std::sin(3.0f * gamma);

	float trueSolarMinutes = decimalHour * 60.0f + equationOfTime + 4.0f * longitudeDeg - 60.0f * timeZone;

	while (trueSolarMinutes < 0.0f)
		trueSolarMinutes += 1440.0f;

	while (trueSolarMinutes >= 1440.0f)
		trueSolarMinutes -= 1440.0f;

	const float hourAngle = (trueSolarMinutes / 4.0f - 180.0f) * DEG2RAD;

	const float cosDeclination = std::cos(declination);
	const float sinDeclination = std::sin(declination);
	const float cosLatitude = std::cos(latitude);
	const float sinLatitude = std::sin(latitude);
	const float cosHourAngle = std::cos(hourAngle);
	const float sinHourAngle = std::sin(hourAngle);

	// Local East-North-Up coordinates.
	const float east = -cosDeclination * sinHourAngle;

	const float north = cosLatitude * sinDeclination - sinLatitude * cosDeclination * cosHourAngle;

	const float up = sinLatitude * sinDeclination + cosLatitude * cosDeclination * cosHourAngle;

	const Vec3f direction = { east, north, up };
	return normalized(direction);
}

float CIEStandardSky(
    int type,
    const Vec3f& direction,
    const Vec3f& toSun,
    float zenithValue)
{
    if (type < 0 || type >= 15)
        type = 0;

    const float* coefficients =
        kCoefficients[type];

    return relativeLuminance(
        direction,
        toSun,
        coefficients[0],
        coefficients[1],
        coefficients[2],
        coefficients[3],
        coefficients[4])
        * zenithValue;
}

CIESkyCoefficients
CIEStandardSkyCoefficients(int type)
{
    if (type < 0 || type >= 15)
        type = 0;

    const float* coefficients =
        kCoefficients[type];

    return {
        coefficients[0],
        coefficients[1],
        coefficients[2],
        coefficients[3],
        coefficients[4]
    };
}

float CIECustomSky(
    const CIESkyCoefficients& coefficients,
    const Vec3f& direction,
    const Vec3f& toSun,
    float zenithValue)
{
    return relativeLuminance(
        direction,
        toSun,
        coefficients.a,
        coefficients.b,
        coefficients.c,
        coefficients.d,
        coefficients.e)
        * zenithValue;
}

} // namespace SSLib
