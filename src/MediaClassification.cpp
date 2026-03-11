#include "MediaClassification.h"

#include <QFileInfo>
#include <QMimeDatabase>
#include <QSet>

namespace {
const QSet<QString> &imageExtensions()
{
    static const QSet<QString> value = {
        QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("png"), QStringLiteral("gif"), QStringLiteral("webp"),
        QStringLiteral("avif"), QStringLiteral("heic"), QStringLiteral("heif"), QStringLiteral("jxl"), QStringLiteral("bmp"),
        QStringLiteral("tif"), QStringLiteral("tiff"), QStringLiteral("ico"), QStringLiteral("icns"),
    };
    return value;
}

const QSet<QString> &videoExtensions()
{
    static const QSet<QString> value = {
        QStringLiteral("mp4"), QStringLiteral("m4v"), QStringLiteral("mov"), QStringLiteral("webm"), QStringLiteral("mkv"),
        QStringLiteral("avi"), QStringLiteral("mpg"), QStringLiteral("mpeg"), QStringLiteral("ts"), QStringLiteral("mts"),
        QStringLiteral("m2ts"), QStringLiteral("flv"), QStringLiteral("ogv"), QStringLiteral("3gp"), QStringLiteral("3g2"),
        QStringLiteral("wmv"), QStringLiteral("asf"), QStringLiteral("mxf"),
    };
    return value;
}

const QSet<QString> &programExtensions()
{
    static const QSet<QString> value = {
        QStringLiteral("dmg"), QStringLiteral("pkg"), QStringLiteral("app"), QStringLiteral("ipa"), QStringLiteral("apk"),
        QStringLiteral("exe"), QStringLiteral("msi"), QStringLiteral("deb"), QStringLiteral("rpm"), QStringLiteral("appimage"),
        QStringLiteral("jar"), QStringLiteral("bin"), QStringLiteral("run"), QStringLiteral("command"), QStringLiteral("bat"),
        QStringLiteral("ps1"),
    };
    return value;
}

const QSet<QString> &archiveExtensions()
{
    static const QSet<QString> value = {
        QStringLiteral("zip"), QStringLiteral("tar"), QStringLiteral("gz"), QStringLiteral("tgz"), QStringLiteral("bz2"),
        QStringLiteral("xz"), QStringLiteral("7z"), QStringLiteral("rar"), QStringLiteral("zst"),
    };
    return value;
}

const QSet<QString> &documentExtensions()
{
    static const QSet<QString> value = {
        QStringLiteral("pdf"), QStringLiteral("txt"), QStringLiteral("md"), QStringLiteral("rtf"), QStringLiteral("doc"),
        QStringLiteral("docx"), QStringLiteral("xls"), QStringLiteral("xlsx"), QStringLiteral("ppt"), QStringLiteral("pptx"),
        QStringLiteral("odt"), QStringLiteral("ods"), QStringLiteral("odp"), QStringLiteral("csv"), QStringLiteral("json"),
        QStringLiteral("xml"), QStringLiteral("yaml"), QStringLiteral("yml"),
    };
    return value;
}

QString normalizedMimeType(const QString &mimeType)
{
    const int separator = mimeType.indexOf(QLatin1Char(';'));
    return (separator >= 0 ? mimeType.left(separator) : mimeType).trimmed().toLower();
}
}

MediaCategory MediaClassification::category(const QString &filename, const QString &mimeType)
{
    const QString extension = filenameExtension(filename);
    if (!extension.isEmpty()) {
        if (imageExtensions().contains(extension)) {
            return MediaCategory::Images;
        }
        if (videoExtensions().contains(extension)) {
            return MediaCategory::Videos;
        }
        if (programExtensions().contains(extension)) {
            return MediaCategory::Programs;
        }
        if (archiveExtensions().contains(extension)) {
            return MediaCategory::Archives;
        }
        if (documentExtensions().contains(extension)) {
            return MediaCategory::Documents;
        }

        QMimeDatabase database;
        const QMimeType type = database.mimeTypeForFile(QStringLiteral("file.") + extension, QMimeDatabase::MatchExtension);
        if (type.name().startsWith(QStringLiteral("image/"))) {
            return MediaCategory::Images;
        }
        if (type.name().startsWith(QStringLiteral("video/"))) {
            return MediaCategory::Videos;
        }
        if (type.name().startsWith(QStringLiteral("audio/"))) {
            return MediaCategory::Audio;
        }
    }

    const QString normalized = normalizedMimeType(mimeType);
    if (normalized.startsWith(QStringLiteral("image/"))) {
        return MediaCategory::Images;
    }
    if (normalized.startsWith(QStringLiteral("video/"))) {
        return MediaCategory::Videos;
    }
    if (normalized.startsWith(QStringLiteral("audio/"))) {
        return MediaCategory::Audio;
    }
    if (normalized == QStringLiteral("application/webm") || normalized == QStringLiteral("application/x-matroska")) {
        return MediaCategory::Videos;
    }
    if (normalized == QStringLiteral("application/pdf") || normalized.startsWith(QStringLiteral("text/"))) {
        return MediaCategory::Documents;
    }
    if (normalized.contains(QStringLiteral("zip")) || normalized.contains(QStringLiteral("compressed")) || normalized.contains(QStringLiteral("archive"))) {
        return MediaCategory::Archives;
    }
    if (normalized.contains(QStringLiteral("msi")) || normalized.contains(QStringLiteral("executable")) || normalized.contains(QStringLiteral("application/x-dosexec"))) {
        return MediaCategory::Programs;
    }

    return MediaCategory::Other;
}

QString MediaClassification::preferredExtension(const QString &filename, const QString &mimeType)
{
    const QString extension = filenameExtension(filename);
    if (!extension.isEmpty()) {
        return extension;
    }

    const QString normalized = normalizedMimeType(mimeType);
    if (normalized.isEmpty()) {
        return {};
    }

    QMimeDatabase database;
    const QMimeType type = database.mimeTypeForName(normalized);
    return type.preferredSuffix().toLower();
}

QString MediaClassification::filenameExtension(const QString &filename)
{
    if (filename.isEmpty()) {
        return {};
    }

    const QString extension = QFileInfo(filename).suffix().toLower();
    return extension;
}

