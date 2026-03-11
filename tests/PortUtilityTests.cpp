#include "Domain.h"
#include "FolderNaming.h"
#include "MediaClassification.h"

#include <QtTest/QTest>

class PortUtilityTests : public QObject
{
    Q_OBJECT

private slots:
    void folderNamingPrefersDisplayNameWhenUnique();
    void folderNamingFallsBackWhenDisplayNameCollides();
    void mediaClassificationRecognizesProgramsAndImages();
    void mediaClassificationDerivesExtensionFromMimeType();
};

void PortUtilityTests::folderNamingPrefersDisplayNameWhenUnique()
{
    const QString label = FolderNaming::preferredLabel(
        QStringLiteral("Goofball"),
        QStringLiteral("#goofball:example.org"),
        {QStringLiteral("#old-goofball:example.org")},
        QStringLiteral("!roomid:example.org"),
        {});

    QCOMPARE(label, QStringLiteral("Goofball"));
}

void PortUtilityTests::folderNamingFallsBackWhenDisplayNameCollides()
{
    const QString label = FolderNaming::preferredLabel(
        QStringLiteral("Goofball"),
        QStringLiteral("#goofball:example.org"),
        {QStringLiteral("#legacy:example.org")},
        QStringLiteral("!roomid:example.org"),
        {QStringLiteral("goofball")});

    QCOMPARE(label, QStringLiteral("#goofball_example.org"));
}

void PortUtilityTests::mediaClassificationRecognizesProgramsAndImages()
{
    QCOMPARE(MediaClassification::category(QStringLiteral("installer.dmg"), {}), MediaCategory::Programs);
    QCOMPARE(MediaClassification::category(QStringLiteral("photo.png"), {}), MediaCategory::Images);
}

void PortUtilityTests::mediaClassificationDerivesExtensionFromMimeType()
{
    QCOMPARE(MediaClassification::preferredExtension({}, QStringLiteral("image/jpeg")), QStringLiteral("jpg"));
}

QTEST_MAIN(PortUtilityTests)

#include "PortUtilityTests.moc"
