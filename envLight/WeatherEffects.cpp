#include "WeatherEffects.h"

#include <algorithm>
#include <QStringList>
#include <QtGlobal>
#include <cmath>

namespace {

double clamp01(double value)
{
    return std::max(0.0, std::min(value, 1.0));
}

bool valid(double value)
{
    return std::isfinite(value);
}

QChar codeAt(const QString& codes, int index)
{
    return index >= 0 && index < codes.size()
        ? codes.at(index)
        : QChar('9');
}

double rainCodeIntensity(QChar code)
{
    switch (code.toLatin1()) {
    case '0': return 0.30; // light rain
    case '1': return 0.62; // moderate rain
    case '2': return 1.00; // heavy rain
    case '3': return 0.36; // light shower
    case '4': return 0.68; // moderate shower
    case '5': return 1.00; // heavy shower
    case '6': return 0.35; // light freezing rain
    case '7': return 0.68; // moderate freezing rain
    case '8': return 1.00; // heavy freezing rain
    default:  return 0.0;
    }
}

double drizzleCodeIntensity(QChar code)
{
    switch (code.toLatin1()) {
    case '0': return 0.55; // light rain squall
    case '1': return 0.82; // moderate rain squall
    case '3': return 0.16; // light drizzle
    case '4': return 0.32; // moderate drizzle
    case '5': return 0.55; // heavy drizzle
    case '6': return 0.22; // light freezing drizzle
    case '7': return 0.42; // moderate freezing drizzle
    case '8': return 0.68; // heavy freezing drizzle
    default:  return 0.0;
    }
}

double snowCodeIntensity(QChar code)
{
    switch (code.toLatin1()) {
    case '0': return 0.28;
    case '1': return 0.62;
    case '2': return 1.00;
    case '3': return 0.30;
    case '4': return 0.65;
    case '5': return 1.00;
    case '6': return 0.22;
    case '7': return 0.48;
    case '8': return 0.80;
    default:  return 0.0;
    }
}

double snowShowerIntensity(QChar code)
{
    switch (code.toLatin1()) {
    case '0': return 0.30;
    case '1': return 0.65;
    case '2': return 1.00;
    case '3': return 0.55;
    case '4': return 0.82;
    case '5': return 1.00;
    case '6': return 0.25;
    case '7': return 0.50;
    default:  return 0.0;
    }
}

double liquidRateIntensity(double millimetresPerHour)
{
    if (millimetresPerHour <= 0.0)
        return 0.0;
    if (millimetresPerHour < 0.5)
        return 0.18;
    if (millimetresPerHour < 2.5)
        return 0.38;
    if (millimetresPerHour < 7.6)
        return 0.72;
    return 1.0;
}

} // namespace

QString precipitationKindName(PrecipitationKind kind)
{
    switch (kind) {
	case PrecipitationKind::Rain:         return QString::fromUtf8("雨");
    case PrecipitationKind::Snow:         return QString::fromUtf8("雪");
    case PrecipitationKind::Mixed:        return QString::fromUtf8("雨夹雪/混合降水");
    case PrecipitationKind::FreezingRain: return QString::fromUtf8("冻雨");
    case PrecipitationKind::Hail:         return QString::fromUtf8("冰雹/冰粒");
    case PrecipitationKind::None:
    default:                              return QString::fromUtf8("无降水");
    }
}

WeatherVisualState deriveWeatherVisualState(
    const EpwRecord& record,
    int recordsPerHour)
{
    WeatherVisualState state;

    state.windDirectionDeg = valid(record.windDirection)
        ? record.windDirection
        : 0.0;
    state.windSpeedMps = valid(record.windSpeed)
        ? std::max(0.0, record.windSpeed)
        : 0.0;
    state.visibilityKm = valid(record.visibility)
        ? record.visibility
        : 9999.0;
    state.snowDepthCm = valid(record.snowDepth)
        ? std::max(0.0, record.snowDepth)
        : 0.0;
    state.groundAlbedo = valid(record.albedo)
        ? clamp01(record.albedo)
        : (state.snowDepthCm > 0.0 ? 0.75 : 0.20);
    state.totalSkyCoverTenths = valid(record.totalSkyCover)
        ? std::max(0.0, std::min(10.0, record.totalSkyCover))
        : 0.0;
    state.opaqueSkyCoverTenths = valid(record.opaqueSkyCover)
        ? std::max(0.0, std::min(10.0, record.opaqueSkyCover))
        : 0.0;

    const bool observationValid =
        record.presentWeatherObservation == 0
        && record.presentWeatherCodes.size() >= 9;

    state.weatherObservationAvailable = observationValid;

    double rainIntensity = 0.0;
    double snowIntensity = 0.0;
    bool freezingRain = false;
    bool hail = false;

    if (observationValid) {
        const QString codes = record.presentWeatherCodes.left(9);

        const QChar rainCode = codeAt(codes, 1);
        const QChar drizzleCode = codeAt(codes, 2);
        const QChar snowCode = codeAt(codes, 3);
        const QChar snowShowerCode = codeAt(codes, 4);
        const QChar sleetHailCode = codeAt(codes, 5);
        const QChar fogCode = codeAt(codes, 6);
        const QChar smokeBlowingCode = codeAt(codes, 7);
        const QChar icePelletCode = codeAt(codes, 8);

        rainIntensity = std::max(
            rainCodeIntensity(rainCode),
            drizzleCodeIntensity(drizzleCode));

        snowIntensity = std::max(
            snowCodeIntensity(snowCode),
            snowShowerIntensity(snowShowerCode));

        freezingRain =
            rainCode == '6' || rainCode == '7' || rainCode == '8'
            || drizzleCode == '6' || drizzleCode == '7' || drizzleCode == '8';

        if (sleetHailCode == '0' || sleetHailCode == '1' || sleetHailCode == '2') {
            snowIntensity = std::max(
                snowIntensity,
                sleetHailCode == '0' ? 0.35
                    : sleetHailCode == '1' ? 0.68 : 1.0);
            rainIntensity = std::max(rainIntensity, 0.25);
        }

        hail = sleetHailCode == '4'
            || icePelletCode == '0'
            || icePelletCode == '1'
            || icePelletCode == '2';

        state.blowingSnow = smokeBlowingCode == '4';
        if (state.blowingSnow)
            snowIntensity = std::max(snowIntensity, 0.50);

        if (fogCode != '9')
            state.fogDensity = std::max(state.fogDensity, 0.45);

        if (fogCode == '5' || fogCode == '7' || fogCode == '8')
            state.fogDensity = std::max(state.fogDensity, 0.78);
    }

    // Liquid precipitation depth is the most useful numeric rain signal.
    if (valid(record.liquidPrecipitationDepth)
        && record.liquidPrecipitationDepth > 0.0) {

        double accumulationHours = record.liquidPrecipitationQuantity;
        if (!valid(accumulationHours) || accumulationHours <= 0.0)
            accumulationHours = 1.0 / std::max(1, recordsPerHour);

        const double rateMmPerHour =
            record.liquidPrecipitationDepth / accumulationHours;

        const double numericIntensity =
            liquidRateIntensity(rateMmPerHour);

        // When explicit snow codes exist, use liquid depth only as an
        // intensity hint. Do not turn coded snowfall into rain merely because
        // a file reports liquid-equivalent precipitation.
        if (snowIntensity > 0.0 && rainIntensity <= 0.0)
            snowIntensity = std::max(snowIntensity, numericIntensity);
        else
            rainIntensity = std::max(rainIntensity, numericIntensity);
    }

    if (valid(record.visibility) && record.visibility < 11.0) {
        const double visibilityFog =
            clamp01((11.0 - record.visibility) / 10.0);
        state.fogDensity = std::max(
            state.fogDensity,
            visibilityFog * 0.85);
    }

    if (hail) {
        state.precipitation = PrecipitationKind::Hail;
        state.intensity = std::max({rainIntensity, snowIntensity, 0.55});
    } else if (freezingRain && rainIntensity > 0.0) {
        state.precipitation = PrecipitationKind::FreezingRain;
        state.intensity = rainIntensity;
    } else if (rainIntensity > 0.0 && snowIntensity > 0.0) {
        state.precipitation = PrecipitationKind::Mixed;
        state.intensity = std::max(rainIntensity, snowIntensity);
    } else if (snowIntensity > 0.0) {
        state.precipitation = PrecipitationKind::Snow;
        state.intensity = snowIntensity;
    } else if (rainIntensity > 0.0) {
        state.precipitation = PrecipitationKind::Rain;
        state.intensity = rainIntensity;
    }

    state.intensity = clamp01(state.intensity);

    QStringList details;
    details << precipitationKindName(state.precipitation);
    if (state.fogDensity > 0.05)
        details << QString::fromUtf8("雾化 %1%").arg(qRound(state.fogDensity * 100.0));
    if (state.snowDepthCm > 0.0)
        details << QString::fromUtf8("积雪 %1 cm").arg(state.snowDepthCm, 0, 'f', 1);
    details << QString::fromUtf8("风 %1 m/s").arg(state.windSpeedMps, 0, 'f', 1);

    state.description = details.join(QString::fromUtf8("，"));
    return state;
}
