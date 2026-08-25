#ifndef EPWDATA_H
#define EPWDATA_H

#include <QString>
#include <QVector>

#include <limits>

inline double epwNaN()
{
    return std::numeric_limits<double>::quiet_NaN();
}

struct EpwLocation {
    QString city;
    QString stateProvince;
    QString country;
    QString source;
    QString wmo;

    double latitude = 0.0;
    double longitude = 0.0;
    double timeZone = 0.0;
    double elevation = 0.0;
};

struct EpwRecord {
    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;   // EPW 1-24, interval end hour
    int minute = 0; // EPW 1-60, interval end minute

    double dryBulb = epwNaN();
    double dewPoint = epwNaN();
    double relativeHumidity = epwNaN();

    // Radiation energy accumulated over the reporting interval, Wh/m2.
    double ghi = epwNaN();
    double dni = epwNaN();
    double dhi = epwNaN();

    // Photometric fields.
    double globalHorizontalIlluminance = epwNaN();  // lx
    double directNormalIlluminance = epwNaN();      // lx
    double diffuseHorizontalIlluminance = epwNaN(); // lx
    double zenithLuminance = epwNaN();              // cd/m2

    // Atmospheric and weather-effect fields.
    double windDirection = epwNaN(); // deg, direction FROM which wind blows
    double windSpeed = epwNaN();     // m/s
    double totalSkyCover = epwNaN();
    double opaqueSkyCover = epwNaN();
    double visibility = epwNaN();    // km
    double ceilingHeight = epwNaN(); // m

    // 0 = observation made; 9 = missing/not made.
    int presentWeatherObservation = 9;
    // Text field containing nine one-digit TMY2-style weather codes.
    QString presentWeatherCodes = QStringLiteral("999999999");

    double precipitableWater = epwNaN();
    double aerosolOpticalDepth = epwNaN();
    double snowDepth = epwNaN();             // cm, snow already on ground
    double daysSinceLastSnowfall = epwNaN();
    double albedo = epwNaN();
    double liquidPrecipitationDepth = epwNaN();    // mm
    double liquidPrecipitationQuantity = epwNaN(); // accumulation period, h
};

struct EpwDocument {
    EpwLocation location;
    QVector<EpwRecord> records;
    int recordsPerHour = 1;
};

#endif // EPWDATA_H
