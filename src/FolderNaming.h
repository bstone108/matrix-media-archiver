#pragma once

#include <QSet>
#include <QString>
#include <QStringList>

class FolderNaming
{
public:
    static QString sanitize(const QString &label);
    static QString preferredLabel(
        const QString &displayName,
        const QString &canonicalAlias,
        const QStringList &rememberedAliases,
        const QString &roomId,
        const QSet<QString> &existingLabels);
};

