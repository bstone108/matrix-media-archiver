#pragma once

#include "Domain.h"

#include <QString>

class MediaClassification
{
public:
    static MediaCategory category(const QString &filename, const QString &mimeType);
    static QString preferredExtension(const QString &filename, const QString &mimeType);
    static QString filenameExtension(const QString &filename);
};

