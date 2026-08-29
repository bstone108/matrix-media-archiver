#pragma once

#include <QString>
#include <optional>

struct DateBuildVersion
{
    int year = 0;
    int month = 0;
    int day = 0;
    int build = 0;

    static std::optional<DateBuildVersion> parse(const QString &raw);
    QString toUnpaddedString() const;
};

int compareDateBuild(const DateBuildVersion &lhs, const DateBuildVersion &rhs);
int compareDateBuildStrings(const QString &lhs, const QString &rhs);
bool isNewerDateBuild(const QString &candidate, const QString &current);
