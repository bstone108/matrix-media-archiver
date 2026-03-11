#include "FolderNaming.h"

#include <QRegularExpression>

QString FolderNaming::sanitize(const QString &label)
{
    QString value = label;
    static const QRegularExpression invalidCharacters(QStringLiteral(R"([\/:\\\?%\*\|"<>\x00-\x1f])"));
    value.replace(invalidCharacters, QStringLiteral("_"));
    value = value.trimmed();

    while (value.endsWith(QLatin1Char('.')) || value.endsWith(QLatin1Char(' '))) {
        value.chop(1);
    }

    while (value.contains(QStringLiteral("__"))) {
        value.replace(QStringLiteral("__"), QStringLiteral("_"));
    }

    if (value.isEmpty()) {
        value = QStringLiteral("_");
    }

    if (value.size() > 120) {
        value = value.left(120);
    }

    return value;
}

QString FolderNaming::preferredLabel(
    const QString &displayName,
    const QString &canonicalAlias,
    const QStringList &rememberedAliases,
    const QString &roomId,
    const QSet<QString> &existingLabels)
{
    QStringList candidates;
    if (!displayName.isEmpty()) {
        candidates.append(displayName);
    }
    if (!canonicalAlias.isEmpty()) {
        candidates.append(canonicalAlias);
    }
    candidates.append(rememberedAliases);

    for (const QString &candidate : candidates) {
        const QString sanitized = sanitize(candidate);
        if (!existingLabels.contains(sanitized.toLower())) {
            return sanitized;
        }
    }

    return sanitize(roomId);
}

