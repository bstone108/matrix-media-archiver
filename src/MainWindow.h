#pragma once

#include "Domain.h"

#include <QMainWindow>

class AppController;
class QCheckBox;
class QComboBox;
class QFormLayout;
class QGroupBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QTableWidget;
class QTextBrowser;

class MainWindow : public QMainWindow
{
public:
    explicit MainWindow(AppController *controller, QWidget *parent = nullptr);

private:
    void refreshView();
    void refreshSelectedRoom();
    void refreshSelectedSpace();
    void chooseDestinationFolder();
    QWidget *buildDashboardPage();
    QWidget *buildWorkersPage();
    QWidget *buildRoomsPage();
    QWidget *buildSpacesPage();
    QWidget *buildQueuePage();
    QWidget *buildHelpPage();
    QWidget *buildSettingsPage();
    QWidget *buildVerificationPage();

    void populateSectionSidebar();
    void populateRoomsList();
    void populateSpacesList();
    void populateQueueTables();
    void populateWorkersPage();
    void populateDashboardPage();
    void populateSettingsPage();
    void populateVerificationPage();
    void updateDetailLabels(
        QListWidget *listWidget,
        QLabel *titleLabel,
        QLabel *idLabel,
        QLabel *aliasLabel,
        QLabel *folderLabel,
        QLabel *watcherLabel,
        QLabel *historyLabel,
        QPlainTextEdit *aliasesTextEdit,
        bool isSpace);
    QString roomDisplayTitle(const QString &roomId) const;
    int workerSlotCount() const;
    AppSettings gatherSettingsFromUi() const;
    bool settingsInputsMatchController() const;

    AppController *controller_;

    QListWidget *sectionList_ = nullptr;
    QStackedWidget *stack_ = nullptr;

    QCheckBox *powerToggle_ = nullptr;
    QLabel *connectionLabel_ = nullptr;
    QLabel *queueLabel_ = nullptr;
    QLabel *loggedInValue_ = nullptr;
    QLabel *accountModeValue_ = nullptr;
    QLabel *joinedRoomsValue_ = nullptr;
    QLabel *joinedSpacesValue_ = nullptr;
    QLabel *activeDownloadsValue_ = nullptr;
    QPlainTextEdit *dashboardLogView_ = nullptr;

    QLabel *workerCountsLabel_ = nullptr;
    QTableWidget *workersTable_ = nullptr;

    QLineEdit *joinRoomEdit_ = nullptr;
    QListWidget *roomsList_ = nullptr;
    QLabel *roomTitleLabel_ = nullptr;
    QLabel *roomIdLabel_ = nullptr;
    QLabel *roomAliasLabel_ = nullptr;
    QLabel *roomFolderLabel_ = nullptr;
    QLabel *roomWatcherLabel_ = nullptr;
    QLabel *roomHistoryLabel_ = nullptr;
    QPlainTextEdit *roomAliasesView_ = nullptr;
    QPushButton *leaveRoomButton_ = nullptr;

    QLineEdit *joinSpaceEdit_ = nullptr;
    QListWidget *spacesList_ = nullptr;
    QLabel *spaceTitleLabel_ = nullptr;
    QLabel *spaceIdLabel_ = nullptr;
    QLabel *spaceAliasLabel_ = nullptr;
    QLabel *spaceFolderLabel_ = nullptr;
    QLabel *spaceWatcherLabel_ = nullptr;
    QLabel *spaceHistoryLabel_ = nullptr;
    QPlainTextEdit *spaceAliasesView_ = nullptr;
    QPushButton *leaveSpaceButton_ = nullptr;

    QLabel *queueStatsLabel_ = nullptr;
    QListWidget *activeDownloadsList_ = nullptr;
    QTableWidget *waitingJobsTable_ = nullptr;
    QTableWidget *failedJobsTable_ = nullptr;

    QLineEdit *homeserverEdit_ = nullptr;
    QLineEdit *usernameEdit_ = nullptr;
    QLineEdit *passwordEdit_ = nullptr;
    QLineEdit *ownerIdEdit_ = nullptr;
    QLineEdit *destinationEdit_ = nullptr;
    QSpinBox *messageLimitSpin_ = nullptr;
    QSpinBox *timeWindowValueSpin_ = nullptr;
    QComboBox *timeWindowUnitCombo_ = nullptr;
    QSpinBox *retryCooldownSpin_ = nullptr;
    QSpinBox *retryLimitSpin_ = nullptr;
    QComboBox *downloadWorkersCombo_ = nullptr;
    QSpinBox *failedRetentionValueSpin_ = nullptr;
    QComboBox *failedRetentionUnitCombo_ = nullptr;
    QPushButton *checkForUpdatesButton_ = nullptr;
    QLabel *settingsVersionValue_ = nullptr;

    QTextBrowser *helpBrowser_ = nullptr;

    QLabel *verificationStatusLabel_ = nullptr;
    QLabel *verificationDeviceIdLabel_ = nullptr;
    QListWidget *verificationEmojiList_ = nullptr;
    QLabel *verificationDecimalsLabel_ = nullptr;

    bool settingsPageInitialized_ = false;
};
