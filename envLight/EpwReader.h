#ifndef EPWREADER_H
#define EPWREADER_H

#include "EpwData.hpp"
#include <QString>

class EpwReader {
public:
    static bool read(const QString& filePath, EpwDocument& doc);
};

#endif // EPWREADER_H