#include "hurricaneparser.h"
#include <QFile>
#include <QTextStream>
#include <QFileInfo>
#include <stdexcept>   // runtime_error  (exception handling)

using namespace std;

// ─────────────────────────────────────────────────────────────────────────────
//  Parses a Stanford / Weather Underground hurricane CSV.
//  Expected schema (header row required):
//      Date, Time, Lat, Lon, Wind, Pressure
//  Wind is in mph. The hurricane name is derived from the file's basename.
// ─────────────────────────────────────────────────────────────────────────────
QVector<HurricanePoint> HurricaneParser::parse(const QString& filePath) {
    QVector<HurricanePoint> points;

    QFile file(filePath);

    // Open failure is unrecoverable here. Throw rather than return an empty
    // vector, which the caller couldn't distinguish from a genuinely empty
    // file — the exception carries the path for a useful error message.
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        throw runtime_error(
            "Could not open file: " + filePath.toStdString());

    // Use the filename (without extension) as the hurricane's name, uppercased.
    const QString name = QFileInfo(filePath).completeBaseName().toUpper();

    QTextStream in(&file);
    bool firstLine = true;

    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith('#'))
            continue;

        // Skip header row
        if (firstLine) {
            firstLine = false;
            continue;
        }

        QStringList cols = line.split(',');
        if (cols.size() < 5)        // Pressure is optional for our purposes
            continue;

        bool latOk = false, lonOk = false, windOk = false;
        const double lat  = cols[2].trimmed().toDouble(&latOk);
        const double lon  = cols[3].trimmed().toDouble(&lonOk);
        const int    wind = cols[4].trimmed().toInt(&windOk);
        if (!latOk || !lonOk || !windOk)
            continue;

        HurricanePoint p;
        p.name       = name;
        p.date       = cols[0].trimmed();
        p.time       = cols[1].trimmed();
        p.lat        = lat;
        p.lon        = lon;
        p.windMph    = wind;
        p.pressureMb = (cols.size() >= 6) ? cols[5].trimmed().toInt() : 0;
        p.category   = computeCategory(p.windMph);
        points.append(p);
    }

    // A readable file that yields zero rows almost always means the wrong
    // schema or a corrupt file. Surface it as an error instead of letting the
    // UI render a blank map and leave the user guessing.
    if (points.isEmpty())
        throw runtime_error(
            "No valid hurricane data found in: " + filePath.toStdString());

    return points;
}
