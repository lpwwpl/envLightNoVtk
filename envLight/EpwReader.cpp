#include "EpwReader.h"

#include <QDebug>
#include <QFile>
#include <QStringList>
#include <QTextStream>

#include <cmath>
#include <limits>

namespace {

double parseNumber(
    const QStringList& fields,
    int index,
    double missingAtOrAbove)
{
    if (index < 0 || index >= fields.size())
        return std::numeric_limits<double>::quiet_NaN();

    bool ok = false;
    const double value =
        fields[index].trimmed().toDouble(&ok);

    if (!ok ||
        !std::isfinite(value) ||
        value >= missingAtOrAbove) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    return value;
}

int parsePositiveInteger(
    const QString& text,
    int fallback)
{
    bool ok = false;
    const int value =
        text.trimmed().toInt(&ok);

    return ok && value > 0
        ? value
        : fallback;
}

} // namespace

bool EpwReader::read(
    const QString& filePath,
    EpwDocument& document)
{
    QFile file(filePath);

    if (!file.open(
            QIODevice::ReadOnly
            | QIODevice::Text)) {
        qWarning()
            << "Cannot open EPW file:"
            << filePath;
        return false;
    }

    QTextStream input(&file);
    document = EpwDocument();

    QStringList headers;

    for (int index = 0; index < 8; ++index) {
        if (input.atEnd()) {
            qWarning()
                << "Incomplete EPW header:"
                << filePath;
            return false;
        }

        headers << input.readLine();
    }

    // LOCATION,city,state,country,source,WMO,
    // latitude,longitude,timeZone,elevation
    const QStringList location =
        headers[0].split(',');

    if (location.size() < 10 ||
        location[0].trimmed().compare(
            "LOCATION",
            Qt::CaseInsensitive) != 0) {
        qWarning()
            << "Invalid EPW LOCATION header:"
            << headers[0];
        return false;
    }

    document.location.city =
        location[1].trimmed();
    document.location.stateProvince =
        location[2].trimmed();
    document.location.country =
        location[3].trimmed();
    document.location.source =
        location[4].trimmed();
    document.location.wmo =
        location[5].trimmed();
    document.location.latitude =
        location[6].trimmed().toDouble();
    document.location.longitude =
        location[7].trimmed().toDouble();
    document.location.timeZone =
        location[8].trimmed().toDouble();
    document.location.elevation =
        location[9].trimmed().toDouble();

    // DATA PERIODS,nPeriods,recordsPerHour,...
    const QStringList dataPeriods =
        headers[7].split(',');

    if (dataPeriods.size() >= 3 &&
        dataPeriods[0].trimmed().compare(
            "DATA PERIODS",
            Qt::CaseInsensitive) == 0) {

        document.recordsPerHour =
            parsePositiveInteger(
                dataPeriods[2],
                1);
    }

    while (!input.atEnd()) {
        const QString line =
            input.readLine();

        if (line.trimmed().isEmpty())
            continue;

        const QStringList fields =
            line.split(',');

        if (fields.size() < 35)
            continue;

        EpwRecord record;

        record.year =
            fields[0].trimmed().toInt();
        record.month =
            fields[1].trimmed().toInt();
        record.day =
            fields[2].trimmed().toInt();
        record.hour =
            fields[3].trimmed().toInt();
        record.minute =
            fields[4].trimmed().toInt();

        record.dryBulb =
            parseNumber(fields, 6, 99.9);
        record.dewPoint =
            parseNumber(fields, 7, 99.9);
        record.relativeHumidity =
            parseNumber(fields, 8, 999.0);

        // Standard EPW zero-based indexes.
        record.ghi =
            parseNumber(fields, 13, 9999.0);
        record.dni =
            parseNumber(fields, 14, 9999.0);
        record.dhi =
            parseNumber(fields, 15, 9999.0);

        record.globalHorizontalIlluminance =
            parseNumber(fields, 16, 999900.0);
        record.directNormalIlluminance =
            parseNumber(fields, 17, 999900.0);
        record.diffuseHorizontalIlluminance =
            parseNumber(fields, 18, 999900.0);
        record.zenithLuminance =
            parseNumber(fields, 19, 9999.0);

        record.windDirection =
            parseNumber(fields, 20, 999.0);
        record.windSpeed =
            parseNumber(fields, 21, 999.0);
        record.totalSkyCover =
            parseNumber(fields, 22, 99.0);
        record.opaqueSkyCover =
            parseNumber(fields, 23, 99.0);
        record.visibility =
            parseNumber(fields, 24, 9999.0);
        record.ceilingHeight =
            parseNumber(fields, 25, 99999.0);

        bool observationOk = false;
        record.presentWeatherObservation =
            fields[26].trimmed().toInt(&observationOk);
        if (!observationOk)
            record.presentWeatherObservation = 9;

        record.presentWeatherCodes =
            fields[27].trimmed();
        if (record.presentWeatherCodes.size() < 9)
            record.presentWeatherCodes = QStringLiteral("999999999");
        else
            record.presentWeatherCodes =
                record.presentWeatherCodes.left(9);

        record.precipitableWater =
            parseNumber(fields, 28, 999.0);
        record.aerosolOpticalDepth =
            parseNumber(fields, 29, 0.999);
        record.snowDepth =
            parseNumber(fields, 30, 999.0);
        record.daysSinceLastSnowfall =
            parseNumber(fields, 31, 99.0);
        record.albedo =
            parseNumber(fields, 32, 999.0);
        record.liquidPrecipitationDepth =
            parseNumber(fields, 33, 999.0);
        record.liquidPrecipitationQuantity =
            parseNumber(fields, 34, 99.0);

        document.records.push_back(record);
    }

    if (document.records.isEmpty()) {
        qWarning()
            << "No valid EPW records:"
            << filePath;
        return false;
    }

    return true;
}
