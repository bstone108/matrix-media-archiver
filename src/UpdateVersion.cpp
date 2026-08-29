#include "UpdateVersion.h"

#include <QStringList>

namespace {
QString stripVersionPrefix(QString raw)
{
    raw = raw.trimmed();
    if (raw.startsWith(QLatin1Char('v')) || raw.startsWith(QLatin1Char('V'))) {
        raw.remove(0, 1);
    }
    if (raw.startsWith(QStringLiteral("refs/tags/"))) {
        raw.remove(0, QStringLiteral("refs/tags/").size());
        if (raw.startsWith(QLatin1Char('v')) || raw.startsWith(QLatin1Char('V'))) {
            raw.remove(0, 1);
        }
    }
    return raw;
}
}

std::optional<DateBuildVersion> DateBuildVersion::parse(const QString &raw)
{
    const QString normalized = stripVersionPrefix(raw);
    const QStringList parts = normalized.split(QLatin1Char('.'));
    if (parts.size() != 4) {
        return std::nullopt;
    }

    DateBuildVersion version;
    bool okYear = false;
    bool okMonth = false;
    bool okDay = false;
    bool okBuild = false;
    version.year = parts[0].toInt(&okYear);
    version.month = parts[1].toInt(&okMonth);
    version.day = parts[2].toInt(&okDay);
    version.build = parts[3].toInt(&okBuild);
    if (!okYear || !okMonth || !okDay || !okBuild) {
        return std::nullopt;
    }
    if (version.year < 1 || version.month < 1 || version.day < 1 || version.build < 1) {
        return std::nullopt;
    }
    return version;
}

QString DateBuildVersion::toUnpaddedString() const
{
    return QStringLiteral("%1.%2.%3.%4").arg(year).arg(month).arg(day).arg(build);
}

int compareDateBuild(const DateBuildVersion &lhs, const DateBuildVersion &rhs)
{
    if (lhs.year != rhs.year) {
        return lhs.year < rhs.year ? -1 : 1;
    }
    if (lhs.month != rhs.month) {
        return lhs.month < rhs.month ? -1 : 1;
    }
    if (lhs.day != rhs.day) {
        return lhs.day < rhs.day ? -1 : 1;
    }
    if (lhs.build != rhs.build) {
        return lhs.build < rhs.build ? -1 : 1;
    }
    return 0;
}

int compareDateBuildStrings(const QString &lhs, const QString &rhs)
{
    const auto left = DateBuildVersion::parse(lhs);
    const auto right = DateBuildVersion::parse(rhs);
    if (!left.has_value() && !right.has_value()) {
        return 0;
    }
    if (!left.has_value()) {
        return -1;
    }
    if (!right.has_value()) {
        return 1;
    }
    return compareDateBuild(*left, *right);
}

bool isNewerDateBuild(const QString &candidate, const QString &current)
{
    return compareDateBuildStrings(candidate, current) > 0;
}
