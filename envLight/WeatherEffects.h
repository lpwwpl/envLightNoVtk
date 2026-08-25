#ifndef WEATHEREFFECTS_H
#define WEATHEREFFECTS_H

#include <QString>

#include "EpwData.hpp"

enum class PrecipitationKind {
    None,
    Rain,
    Snow,
    Mixed,
    FreezingRain,
    Hail
};

struct WeatherVisualState {
    PrecipitationKind precipitation = PrecipitationKind::None;

    // 0..1 visual intensity. This is derived from EPW weather codes and,
    // for rain, liquid precipitation rate when available.
    double intensity = 0.0;

    // 0..1 atmospheric whitening/visibility attenuation.
    double fogDensity = 0.0;

    // EPW meteorological convention: direction FROM which wind blows.
    // 0 North, 90 East, 180 South, 270 West.
    double windDirectionDeg = 0.0;
    double windSpeedMps = 0.0;

    double visibilityKm = 9999.0;
    double snowDepthCm = 0.0;
    double groundAlbedo = 0.2;
    double totalSkyCoverTenths = 0.0;
    double opaqueSkyCoverTenths = 0.0;

    bool blowingSnow = false;
    bool weatherObservationAvailable = false;
    QString description;
};

WeatherVisualState deriveWeatherVisualState(
    const EpwRecord& record,
    int recordsPerHour);

QString precipitationKindName(PrecipitationKind kind);

#endif // WEATHEREFFECTS_H
