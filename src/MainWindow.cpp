#include "MainWindow.h"

#include "AppController.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QFileDialog>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextBrowser>
#include <QVBoxLayout>
#include <QHeaderView>

namespace {
bool settingsEqual(const AppSettings &lhs, const AppSettings &rhs)
{
    return lhs.homeserverUrl == rhs.homeserverUrl
        && lhs.username == rhs.username
        && lhs.ownerUserId == rhs.ownerUserId
        && lhs.destinationRootPath == rhs.destinationRootPath
        && lhs.messageLimit == rhs.messageLimit
        && lhs.timeWindowValue == rhs.timeWindowValue
        && lhs.timeWindowUnit == rhs.timeWindowUnit
        && lhs.retryCooldownMinutes == rhs.retryCooldownMinutes
        && lhs.retryLimit == rhs.retryLimit
        && lhs.downloadWorkerCount == rhs.downloadWorkerCount
        && lhs.failedJobRetentionValue == rhs.failedJobRetentionValue
        && lhs.failedJobRetentionUnit == rhs.failedJobRetentionUnit
        && lhs.desiredPowerState == rhs.desiredPowerState;
}
}

MainWindow::MainWindow(AppController *controller, QWidget *parent)
    : QMainWindow(parent)
    , controller_(controller)
{
    setWindowTitle(QStringLiteral("Matrix Media"));
    resize(1320, 820);

    auto *central = new QWidget(this);
    auto *layout = new QHBoxLayout(central);

    sectionList_ = new QListWidget(central);
    sectionList_->setFixedWidth(220);
    stack_ = new QStackedWidget(central);

    populateSectionSidebar();
    stack_->addWidget(buildDashboardPage());
    stack_->addWidget(buildWorkersPage());
    stack_->addWidget(buildRoomsPage());
    stack_->addWidget(buildSpacesPage());
    stack_->addWidget(buildQueuePage());
    stack_->addWidget(buildHelpPage());
    stack_->addWidget(buildSettingsPage());
    stack_->addWidget(buildVerificationPage());

    layout->addWidget(sectionList_);
    layout->addWidget(stack_, 1);
    setCentralWidget(central);

    connect(sectionList_, &QListWidget::currentRowChanged, stack_, &QStackedWidget::setCurrentIndex);
    connect(controller_, &AppController::stateChanged, this, &MainWindow::refreshView);
    sectionList_->setCurrentRow(0);
    refreshView();
}

void MainWindow::refreshView()
{
    populateDashboardPage();
    populateWorkersPage();
    populateRoomsList();
    populateSpacesList();
    populateQueueTables();
    populateSettingsPage();
    populateVerificationPage();
    refreshSelectedRoom();
    refreshSelectedSpace();

    if (!controller_->lastErrorMessage().isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Matrix Media"), controller_->lastErrorMessage());
        controller_->dismissError();
    }
}

void MainWindow::refreshSelectedRoom()
{
    updateDetailLabels(
        roomsList_,
        roomTitleLabel_,
        roomIdLabel_,
        roomAliasLabel_,
        roomFolderLabel_,
        roomWatcherLabel_,
        roomHistoryLabel_,
        roomAliasesView_,
        false);
}

void MainWindow::refreshSelectedSpace()
{
    updateDetailLabels(
        spacesList_,
        spaceTitleLabel_,
        spaceIdLabel_,
        spaceAliasLabel_,
        spaceFolderLabel_,
        spaceWatcherLabel_,
        spaceHistoryLabel_,
        spaceAliasesView_,
        true);
}

void MainWindow::chooseDestinationFolder()
{
    const QString current = destinationEdit_->text().trimmed();
    const QString selected = QFileDialog::getExistingDirectory(this, QStringLiteral("Choose Destination Root"), current);
    if (!selected.isEmpty()) {
        destinationEdit_->setText(selected);
    }
}

QWidget *MainWindow::buildDashboardPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);

    auto *topRow = new QHBoxLayout();
    powerToggle_ = new QCheckBox(QStringLiteral("Power"), page);
    connectionLabel_ = new QLabel(page);
    queueLabel_ = new QLabel(page);
    topRow->addWidget(powerToggle_);
    topRow->addWidget(connectionLabel_);
    topRow->addWidget(queueLabel_);
    topRow->addStretch();
    layout->addLayout(topRow);

    auto *statsBox = new QGroupBox(QStringLiteral("Runtime"), page);
    auto *statsLayout = new QGridLayout(statsBox);
    loggedInValue_ = new QLabel(statsBox);
    accountModeValue_ = new QLabel(statsBox);
    joinedRoomsValue_ = new QLabel(statsBox);
    joinedSpacesValue_ = new QLabel(statsBox);
    activeDownloadsValue_ = new QLabel(statsBox);

    statsLayout->addWidget(new QLabel(QStringLiteral("Logged In"), statsBox), 0, 0);
    statsLayout->addWidget(loggedInValue_, 0, 1);
    statsLayout->addWidget(new QLabel(QStringLiteral("Account Mode"), statsBox), 0, 2);
    statsLayout->addWidget(accountModeValue_, 0, 3);
    statsLayout->addWidget(new QLabel(QStringLiteral("Joined Rooms"), statsBox), 1, 0);
    statsLayout->addWidget(joinedRoomsValue_, 1, 1);
    statsLayout->addWidget(new QLabel(QStringLiteral("Spaces"), statsBox), 1, 2);
    statsLayout->addWidget(joinedSpacesValue_, 1, 3);
    statsLayout->addWidget(new QLabel(QStringLiteral("Active Downloads"), statsBox), 2, 0);
    statsLayout->addWidget(activeDownloadsValue_, 2, 1);
    layout->addWidget(statsBox);

    auto *logBox = new QGroupBox(QStringLiteral("Log"), page);
    auto *logLayout = new QVBoxLayout(logBox);
    dashboardLogView_ = new QPlainTextEdit(logBox);
    dashboardLogView_->setReadOnly(true);
    logLayout->addWidget(dashboardLogView_);
    layout->addWidget(logBox, 1);

    connect(powerToggle_, &QCheckBox::toggled, controller_, &AppController::togglePower);
    return page;
}

QWidget *MainWindow::buildWorkersPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    workerCountsLabel_ = new QLabel(page);
    workersTable_ = new QTableWidget(page);
    workersTable_->setColumnCount(3);
    workersTable_->setHorizontalHeaderLabels({QStringLiteral("Room"), QStringLiteral("Watcher"), QStringLiteral("History")});
    workersTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    workersTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    workersTable_->setSelectionMode(QAbstractItemView::NoSelection);

    layout->addWidget(workerCountsLabel_);
    layout->addWidget(workersTable_, 1);
    return page;
}

QWidget *MainWindow::buildRoomsPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    auto *splitter = new QSplitter(Qt::Horizontal, page);

    auto *left = new QWidget(splitter);
    auto *leftLayout = new QVBoxLayout(left);
    auto *joinRow = new QHBoxLayout();
    joinRoomEdit_ = new QLineEdit(left);
    joinRoomEdit_->setPlaceholderText(QStringLiteral("!room:server or #alias:server"));
    auto *joinButton = new QPushButton(QStringLiteral("Join"), left);
    joinRow->addWidget(joinRoomEdit_);
    joinRow->addWidget(joinButton);
    roomsList_ = new QListWidget(left);
    leftLayout->addLayout(joinRow);
    leftLayout->addWidget(roomsList_, 1);

    auto *right = new QWidget(splitter);
    auto *rightLayout = new QVBoxLayout(right);
    roomTitleLabel_ = new QLabel(right);
    roomIdLabel_ = new QLabel(right);
    roomAliasLabel_ = new QLabel(right);
    roomFolderLabel_ = new QLabel(right);
    roomWatcherLabel_ = new QLabel(right);
    roomHistoryLabel_ = new QLabel(right);
    leaveRoomButton_ = new QPushButton(QStringLiteral("Leave Room"), right);
    roomAliasesView_ = new QPlainTextEdit(right);
    roomAliasesView_->setReadOnly(true);

    rightLayout->addWidget(roomTitleLabel_);
    rightLayout->addWidget(roomIdLabel_);
    rightLayout->addWidget(roomAliasLabel_);
    rightLayout->addWidget(roomFolderLabel_);
    rightLayout->addWidget(roomWatcherLabel_);
    rightLayout->addWidget(roomHistoryLabel_);
    rightLayout->addWidget(new QLabel(QStringLiteral("Known Aliases"), right));
    rightLayout->addWidget(roomAliasesView_, 1);
    rightLayout->addWidget(leaveRoomButton_);

    splitter->addWidget(left);
    splitter->addWidget(right);
    splitter->setStretchFactor(1, 1);
    layout->addWidget(splitter);

    connect(joinButton, &QPushButton::clicked, this, [this]() {
        controller_->joinRoom(joinRoomEdit_->text());
    });
    connect(roomsList_, &QListWidget::currentRowChanged, this, &MainWindow::refreshSelectedRoom);
    connect(leaveRoomButton_, &QPushButton::clicked, this, [this]() {
        const QListWidgetItem *item = roomsList_->currentItem();
        if (item != nullptr) {
            controller_->leaveRoom(item->data(Qt::UserRole).toString());
        }
    });
    return page;
}

QWidget *MainWindow::buildSpacesPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    auto *splitter = new QSplitter(Qt::Horizontal, page);

    auto *left = new QWidget(splitter);
    auto *leftLayout = new QVBoxLayout(left);
    auto *joinRow = new QHBoxLayout();
    joinSpaceEdit_ = new QLineEdit(left);
    joinSpaceEdit_->setPlaceholderText(QStringLiteral("!space:server or #alias:server"));
    auto *joinButton = new QPushButton(QStringLiteral("Join"), left);
    joinRow->addWidget(joinSpaceEdit_);
    joinRow->addWidget(joinButton);
    spacesList_ = new QListWidget(left);
    leftLayout->addLayout(joinRow);
    leftLayout->addWidget(spacesList_, 1);

    auto *right = new QWidget(splitter);
    auto *rightLayout = new QVBoxLayout(right);
    spaceTitleLabel_ = new QLabel(right);
    spaceIdLabel_ = new QLabel(right);
    spaceAliasLabel_ = new QLabel(right);
    spaceFolderLabel_ = new QLabel(right);
    spaceWatcherLabel_ = new QLabel(right);
    spaceHistoryLabel_ = new QLabel(right);
    leaveSpaceButton_ = new QPushButton(QStringLiteral("Leave Space"), right);
    spaceAliasesView_ = new QPlainTextEdit(right);
    spaceAliasesView_->setReadOnly(true);

    rightLayout->addWidget(spaceTitleLabel_);
    rightLayout->addWidget(spaceIdLabel_);
    rightLayout->addWidget(spaceAliasLabel_);
    rightLayout->addWidget(spaceFolderLabel_);
    rightLayout->addWidget(spaceWatcherLabel_);
    rightLayout->addWidget(spaceHistoryLabel_);
    rightLayout->addWidget(new QLabel(QStringLiteral("Known Aliases"), right));
    rightLayout->addWidget(spaceAliasesView_, 1);
    rightLayout->addWidget(leaveSpaceButton_);

    splitter->addWidget(left);
    splitter->addWidget(right);
    splitter->setStretchFactor(1, 1);
    layout->addWidget(splitter);

    connect(joinButton, &QPushButton::clicked, this, [this]() {
        controller_->joinRoom(joinSpaceEdit_->text());
    });
    connect(spacesList_, &QListWidget::currentRowChanged, this, &MainWindow::refreshSelectedSpace);
    connect(leaveSpaceButton_, &QPushButton::clicked, this, [this]() {
        const QListWidgetItem *item = spacesList_->currentItem();
        if (item != nullptr) {
            controller_->leaveRoom(item->data(Qt::UserRole).toString());
        }
    });
    return page;
}

QWidget *MainWindow::buildQueuePage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    queueStatsLabel_ = new QLabel(page);

    auto *activeBox = new QGroupBox(QStringLiteral("Active Downloads"), page);
    auto *activeLayout = new QVBoxLayout(activeBox);
    activeDownloadsList_ = new QListWidget(activeBox);
    activeLayout->addWidget(activeDownloadsList_);

    auto *waitingBox = new QGroupBox(QStringLiteral("Waiting"), page);
    auto *waitingLayout = new QVBoxLayout(waitingBox);
    waitingJobsTable_ = new QTableWidget(waitingBox);
    waitingJobsTable_->setColumnCount(4);
    waitingJobsTable_->setHorizontalHeaderLabels({QStringLiteral("File"), QStringLiteral("Room"), QStringLiteral("State"), QStringLiteral("Error")});
    waitingJobsTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    waitingJobsTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    waitingLayout->addWidget(waitingJobsTable_);

    auto *failedBox = new QGroupBox(QStringLiteral("Failed"), page);
    auto *failedLayout = new QVBoxLayout(failedBox);
    auto *failedButtons = new QHBoxLayout();
    auto *retryAllButton = new QPushButton(QStringLiteral("Retry All"), failedBox);
    auto *clearAllButton = new QPushButton(QStringLiteral("Clear All"), failedBox);
    failedButtons->addWidget(retryAllButton);
    failedButtons->addWidget(clearAllButton);
    failedButtons->addStretch();
    failedJobsTable_ = new QTableWidget(failedBox);
    failedJobsTable_->setColumnCount(5);
    failedJobsTable_->setHorizontalHeaderLabels({QStringLiteral("ID"), QStringLiteral("File"), QStringLiteral("Room"), QStringLiteral("Error"), QStringLiteral("Updated")});
    failedJobsTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    failedJobsTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    failedLayout->addLayout(failedButtons);
    failedLayout->addWidget(failedJobsTable_);

    layout->addWidget(queueStatsLabel_);
    layout->addWidget(activeBox);
    layout->addWidget(waitingBox, 1);
    layout->addWidget(failedBox, 1);

    connect(retryAllButton, &QPushButton::clicked, controller_, &AppController::retryAllFailedJobs);
    connect(clearAllButton, &QPushButton::clicked, controller_, &AppController::clearAllFailedJobs);
    return page;
}

QWidget *MainWindow::buildHelpPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    helpBrowser_ = new QTextBrowser(page);
    helpBrowser_->setOpenExternalLinks(false);
    helpBrowser_->setHtml(QStringLiteral(R"(
        <h2>Chat Commands</h2>
        <p>Only one in-chat command is currently implemented:</p>
        <pre>!matrixdl join &lt;room-id-or-alias&gt;</pre>
        <p>Example: <code>!matrixdl join #goofball:example.org</code></p>
        <h2>How Commands Are Handled</h2>
        <ul>
          <li>Commands are only accepted from the Owner Matrix ID in Settings.</li>
          <li>Command prefix is <code>!matrixdl</code>.</li>
          <li>The app logs each command and whether it was followed.</li>
          <li>Dedicated bot mode can reply to the owner via DM.</li>
          <li>Shared-owner-account mode keeps command results local to the app log.</li>
        </ul>
        <h2>Current Limitations</h2>
        <ul>
          <li>No other chat commands are implemented yet.</li>
          <li>Room joins by plain display name are not supported.</li>
          <li>Use a room alias (<code>#room:server</code>) or room ID (<code>!id:server</code>).</li>
        </ul>
    )"));
    layout->addWidget(helpBrowser_);
    return page;
}

QWidget *MainWindow::buildSettingsPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    auto *form = new QGridLayout();

    settingsVersionValue_ = new QLabel(page);
    homeserverEdit_ = new QLineEdit(page);
    usernameEdit_ = new QLineEdit(page);
    passwordEdit_ = new QLineEdit(page);
    passwordEdit_->setEchoMode(QLineEdit::Password);
    ownerIdEdit_ = new QLineEdit(page);
    destinationEdit_ = new QLineEdit(page);
    auto *chooseButton = new QPushButton(QStringLiteral("Choose…"), page);
    messageLimitSpin_ = new QSpinBox(page);
    messageLimitSpin_->setMaximum(1000000);
    timeWindowValueSpin_ = new QSpinBox(page);
    timeWindowValueSpin_->setMaximum(1000000);
    timeWindowUnitCombo_ = new QComboBox(page);
    retryCooldownSpin_ = new QSpinBox(page);
    retryCooldownSpin_->setMaximum(1000000);
    retryLimitSpin_ = new QSpinBox(page);
    retryLimitSpin_->setMaximum(1000000);
    downloadWorkersCombo_ = new QComboBox(page);
    failedRetentionValueSpin_ = new QSpinBox(page);
    failedRetentionValueSpin_->setMaximum(1000000);
    failedRetentionUnitCombo_ = new QComboBox(page);

    for (const TimeWindowUnit unit : allTimeWindowUnits()) {
        timeWindowUnitCombo_->addItem(timeWindowUnitTitle(unit), static_cast<int>(unit));
    }
    for (int count = 1; count <= 6; ++count) {
        downloadWorkersCombo_->addItem(QString::number(count), count);
    }
    for (const FailedJobRetentionUnit unit : allFailedJobRetentionUnits()) {
        failedRetentionUnitCombo_->addItem(failedJobRetentionUnitTitle(unit), static_cast<int>(unit));
    }

    int row = 0;
    form->addWidget(new QLabel(QStringLiteral("App Version"), page), row, 0);
    form->addWidget(settingsVersionValue_, row++, 1, 1, 2);
    form->addWidget(new QLabel(QStringLiteral("Homeserver URL"), page), row, 0);
    form->addWidget(homeserverEdit_, row++, 1, 1, 2);
    form->addWidget(new QLabel(QStringLiteral("Username"), page), row, 0);
    form->addWidget(usernameEdit_, row++, 1, 1, 2);
    form->addWidget(new QLabel(QStringLiteral("Password"), page), row, 0);
    form->addWidget(passwordEdit_, row++, 1, 1, 2);
    form->addWidget(new QLabel(QStringLiteral("Owner Matrix ID"), page), row, 0);
    form->addWidget(ownerIdEdit_, row++, 1, 1, 2);
    form->addWidget(new QLabel(QStringLiteral("Destination Root"), page), row, 0);
    form->addWidget(destinationEdit_, row, 1);
    form->addWidget(chooseButton, row++, 2);
    form->addWidget(new QLabel(QStringLiteral("Message Limit"), page), row, 0);
    form->addWidget(messageLimitSpin_, row++, 1, 1, 2);
    form->addWidget(new QLabel(QStringLiteral("Time Window Value"), page), row, 0);
    form->addWidget(timeWindowValueSpin_, row++, 1, 1, 2);
    form->addWidget(new QLabel(QStringLiteral("Time Window Unit"), page), row, 0);
    form->addWidget(timeWindowUnitCombo_, row++, 1, 1, 2);
    form->addWidget(new QLabel(QStringLiteral("Retry Cooldown Minutes"), page), row, 0);
    form->addWidget(retryCooldownSpin_, row++, 1, 1, 2);
    form->addWidget(new QLabel(QStringLiteral("Retry Limit"), page), row, 0);
    form->addWidget(retryLimitSpin_, row++, 1, 1, 2);
    form->addWidget(new QLabel(QStringLiteral("Download Workers"), page), row, 0);
    form->addWidget(downloadWorkersCombo_, row++, 1, 1, 2);
    form->addWidget(new QLabel(QStringLiteral("Auto Clear Failed After"), page), row, 0);
    form->addWidget(failedRetentionValueSpin_, row++, 1, 1, 2);
    form->addWidget(new QLabel(QStringLiteral("Failed Retention Unit"), page), row, 0);
    form->addWidget(failedRetentionUnitCombo_, row++, 1, 1, 2);

    auto *saveButton = new QPushButton(QStringLiteral("Save Settings"), page);
    auto *resetButton = new QPushButton(QStringLiteral("Reset History Scans"), page);
    auto *notes = new QLabel(
        QStringLiteral("Permanent failures can be auto-cleared after a set number of minutes, hours, or days. "
                       "Set the unit to Disabled or the value to 0 to keep them indefinitely.\n\n"
                       "Use Reset History Scans after changing message limit or time window if you want joined rooms rescanned "
                       "and discoveries rebuilt from scratch. Existing files stay on disk and matching hashes should still be skipped."),
        page);
    notes->setWordWrap(true);

    layout->addLayout(form);
    layout->addWidget(saveButton);
    layout->addWidget(resetButton);
    layout->addWidget(notes);
    layout->addStretch();

    connect(chooseButton, &QPushButton::clicked, this, &MainWindow::chooseDestinationFolder);
    connect(saveButton, &QPushButton::clicked, this, [this]() {
        controller_->saveSettings(gatherSettingsFromUi(), passwordEdit_->text());
    });
    connect(resetButton, &QPushButton::clicked, controller_, &AppController::resetHistoryScans);
    return page;
}

QWidget *MainWindow::buildVerificationPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    verificationStatusLabel_ = new QLabel(page);
    verificationDeviceIdLabel_ = new QLabel(page);
    verificationEmojiList_ = new QListWidget(page);
    verificationDecimalsLabel_ = new QLabel(page);

    auto *buttonRow = new QHBoxLayout();
    auto *requestButton = new QPushButton(QStringLiteral("Request Verification"), page);
    auto *startButton = new QPushButton(QStringLiteral("Start SAS"), page);
    auto *approveButton = new QPushButton(QStringLiteral("Approve"), page);
    auto *rejectButton = new QPushButton(QStringLiteral("Reject"), page);
    buttonRow->addWidget(requestButton);
    buttonRow->addWidget(startButton);
    buttonRow->addWidget(approveButton);
    buttonRow->addWidget(rejectButton);
    buttonRow->addStretch();

    layout->addWidget(verificationStatusLabel_);
    layout->addWidget(verificationDeviceIdLabel_);
    layout->addLayout(buttonRow);
    layout->addWidget(new QLabel(QStringLiteral("Emoji Verification"), page));
    layout->addWidget(verificationEmojiList_);
    layout->addWidget(verificationDecimalsLabel_);
    layout->addStretch();

    connect(requestButton, &QPushButton::clicked, controller_, &AppController::requestVerification);
    connect(startButton, &QPushButton::clicked, controller_, &AppController::startSasVerification);
    connect(approveButton, &QPushButton::clicked, controller_, &AppController::approveVerification);
    connect(rejectButton, &QPushButton::clicked, controller_, &AppController::declineVerification);
    return page;
}

void MainWindow::populateSectionSidebar()
{
    for (const AppSection section : allSections()) {
        auto *item = new QListWidgetItem(sectionTitle(section), sectionList_);
        item->setData(Qt::UserRole, static_cast<int>(section));
    }
}

void MainWindow::populateRoomsList()
{
    const QString currentRoomId = roomsList_->currentItem() ? roomsList_->currentItem()->data(Qt::UserRole).toString() : QString {};
    roomsList_->clear();
    for (const RoomRecord &room : controller_->joinedRooms()) {
        auto *item = new QListWidgetItem(roomDisplayTitle(room.roomId), roomsList_);
        item->setData(Qt::UserRole, room.roomId);
        item->setToolTip(room.roomId);
        if (room.roomId == currentRoomId) {
            roomsList_->setCurrentItem(item);
        }
    }
    if (roomsList_->currentRow() < 0 && roomsList_->count() > 0) {
        roomsList_->setCurrentRow(0);
    }
}

void MainWindow::populateSpacesList()
{
    const QString currentSpaceId = spacesList_->currentItem() ? spacesList_->currentItem()->data(Qt::UserRole).toString() : QString {};
    spacesList_->clear();
    for (const RoomRecord &room : controller_->joinedSpaces()) {
        auto *item = new QListWidgetItem(roomDisplayTitle(room.roomId), spacesList_);
        item->setData(Qt::UserRole, room.roomId);
        item->setToolTip(room.roomId);
        if (room.roomId == currentSpaceId) {
            spacesList_->setCurrentItem(item);
        }
    }
    if (spacesList_->currentRow() < 0 && spacesList_->count() > 0) {
        spacesList_->setCurrentRow(0);
    }
}

void MainWindow::populateQueueTables()
{
    const QVector<DownloadJobRecord> jobs = controller_->jobs();
    const QVector<ActiveDownloadSnapshot> activeDownloads = controller_->runtime().activeDownloads;
    const int activeWorkerSlots = workerSlotCount();

    queueStatsLabel_->setText(QStringLiteral("Items Waiting: %1   Failed: %2   Active: %3/%4")
        .arg(controller_->waitingQueueCount())
        .arg([&jobs]() {
            int count = 0;
            for (const DownloadJobRecord &job : jobs) {
                if (job.state == DownloadJobState::FailedPermanent) {
                    ++count;
                }
            }
            return count;
        }())
        .arg(activeDownloads.size())
        .arg(controller_->settings().downloadWorkerCount));

    activeDownloadsList_->clear();
    for (int workerId = 1; workerId <= activeWorkerSlots; ++workerId) {
        bool found = false;
        for (const ActiveDownloadSnapshot &download : activeDownloads) {
            if (download.workerId == workerId) {
                const QString progress = download.totalBytes > 0
                    ? QStringLiteral("%1 of %2 bytes").arg(download.receivedBytes).arg(download.totalBytes)
                    : QStringLiteral("%1 bytes received").arg(download.receivedBytes);
                activeDownloadsList_->addItem(QStringLiteral("Downloader %1: %2 [%3] %4")
                    .arg(workerId)
                    .arg(download.filename)
                    .arg(roomDisplayTitle(download.roomId))
                    .arg(progress));
                found = true;
                break;
            }
        }
        if (!found) {
            activeDownloadsList_->addItem(QStringLiteral("Downloader %1: Idle").arg(workerId));
        }
    }

    QVector<DownloadJobRecord> waitingJobs;
    QVector<DownloadJobRecord> failedJobs;
    for (const DownloadJobRecord &job : jobs) {
        if (job.state == DownloadJobState::FailedPermanent) {
            failedJobs.append(job);
        } else if (job.state == DownloadJobState::Queued
            || job.state == DownloadJobState::CoolingDown
            || job.state == DownloadJobState::UndecryptablePending) {
            waitingJobs.append(job);
        }
    }

    waitingJobsTable_->setRowCount(waitingJobs.size());
    for (int row = 0; row < waitingJobs.size(); ++row) {
        const DownloadJobRecord &job = waitingJobs.at(row);
        waitingJobsTable_->setItem(row, 0, new QTableWidgetItem(job.originalFilename.isEmpty() ? job.eventId : job.originalFilename));
        waitingJobsTable_->setItem(row, 1, new QTableWidgetItem(roomDisplayTitle(job.roomId)));
        waitingJobsTable_->setItem(row, 2, new QTableWidgetItem(downloadJobStateTitle(job.state)));
        waitingJobsTable_->setItem(row, 3, new QTableWidgetItem(job.lastError));
    }

    failedJobsTable_->setRowCount(failedJobs.size());
    for (int row = 0; row < failedJobs.size(); ++row) {
        const DownloadJobRecord &job = failedJobs.at(row);
        failedJobsTable_->setItem(row, 0, new QTableWidgetItem(QString::number(job.id)));
        failedJobsTable_->setItem(row, 1, new QTableWidgetItem(job.originalFilename.isEmpty() ? job.eventId : job.originalFilename));
        failedJobsTable_->setItem(row, 2, new QTableWidgetItem(roomDisplayTitle(job.roomId)));
        failedJobsTable_->setItem(row, 3, new QTableWidgetItem(job.lastError));
        failedJobsTable_->setItem(
            row,
            4,
            new QTableWidgetItem(
                job.updatedAt.isValid()
                    ? QLocale().toString(job.updatedAt.toLocalTime(), QLocale::ShortFormat)
                    : QStringLiteral("-")));
    }
}

void MainWindow::populateWorkersPage()
{
    const QVector<RoomWorkerSnapshot> workers = controller_->runtime().workerStates;
    int liveWatchers = 0;
    int historyTasks = 0;
    for (const RoomWorkerSnapshot &worker : workers) {
        if (worker.liveWatcherActive) {
            ++liveWatchers;
        }
        if (worker.historyMode != RoomHistoryMode::Idle && worker.historyMode != RoomHistoryMode::Complete) {
            ++historyTasks;
        }
    }

    workerCountsLabel_->setText(QStringLiteral("Active Workers: %1   Live Watchers: %2   History Tasks: %3")
        .arg(workers.size())
        .arg(liveWatchers)
        .arg(historyTasks));

    workersTable_->setRowCount(workers.size());
    for (int row = 0; row < workers.size(); ++row) {
        const RoomWorkerSnapshot &worker = workers.at(row);
        workersTable_->setItem(row, 0, new QTableWidgetItem(roomDisplayTitle(worker.roomId)));
        workersTable_->setItem(row, 1, new QTableWidgetItem(worker.liveWatcherActive ? QStringLiteral("Watching") : QStringLiteral("Paused")));
        workersTable_->setItem(row, 2, new QTableWidgetItem(roomHistoryModeTitle(worker.historyMode) + QStringLiteral(" · ") + worker.historyDetail));
    }
}

void MainWindow::populateDashboardPage()
{
    const BotRuntimeSnapshot runtime = controller_->runtime();
    powerToggle_->blockSignals(true);
    powerToggle_->setChecked(controller_->settings().desiredPowerState);
    powerToggle_->blockSignals(false);

    connectionLabel_->setText(QStringLiteral("Status: %1").arg(controller_->connectionStatusText()));
    queueLabel_->setText(QStringLiteral("Queue: %1").arg(controller_->waitingQueueCount()));
    loggedInValue_->setText(runtime.currentUserId.isEmpty() ? QStringLiteral("Not connected") : runtime.currentUserId);
    accountModeValue_->setText(runtime.accountMode.isEmpty() ? QStringLiteral("Unknown") : runtime.accountMode);
    joinedRoomsValue_->setText(QString::number(controller_->joinedRooms().size()));
    joinedSpacesValue_->setText(QString::number(controller_->joinedSpaces().size()));

    QString activeDownloadsTitle = QStringLiteral("0");
    if (!runtime.activeDownloads.isEmpty()) {
        if (runtime.activeDownloads.size() == 1) {
            activeDownloadsTitle = roomDisplayTitle(runtime.activeDownloads.first().roomId);
        } else {
            activeDownloadsTitle = QString::number(runtime.activeDownloads.size());
        }
    }
    activeDownloadsValue_->setText(activeDownloadsTitle);

    QStringList lines;
    for (const ActivityLogEntry &entry : controller_->visibleLogs()) {
        const QString timestamp = entry.createdAt.isValid()
            ? entry.createdAt.toLocalTime().toString(QStringLiteral("HH:mm:ss"))
            : QStringLiteral("--:--:--");
        lines.append(QStringLiteral("%1  [%2] %3").arg(timestamp, entry.subsystem, entry.message));
    }
    dashboardLogView_->setPlainText(lines.join(QLatin1Char('\n')));
}

void MainWindow::populateSettingsPage()
{
    if (settingsPageInitialized_ && !settingsInputsMatchController()) {
        return;
    }

    const AppSettings &settings = controller_->settings();
    settingsVersionValue_->setText(QCoreApplication::applicationVersion());
    homeserverEdit_->setText(settings.homeserverUrl);
    usernameEdit_->setText(settings.username);
    passwordEdit_->setText(controller_->password());
    ownerIdEdit_->setText(settings.ownerUserId);
    destinationEdit_->setText(settings.destinationRootPath);
    messageLimitSpin_->setValue(settings.messageLimit);
    timeWindowValueSpin_->setValue(settings.timeWindowValue);
    timeWindowUnitCombo_->setCurrentIndex(timeWindowUnitCombo_->findData(static_cast<int>(settings.timeWindowUnit)));
    retryCooldownSpin_->setValue(settings.retryCooldownMinutes);
    retryLimitSpin_->setValue(settings.retryLimit);
    downloadWorkersCombo_->setCurrentIndex(downloadWorkersCombo_->findData(settings.downloadWorkerCount));
    failedRetentionValueSpin_->setValue(settings.failedJobRetentionValue);
    failedRetentionUnitCombo_->setCurrentIndex(failedRetentionUnitCombo_->findData(static_cast<int>(settings.failedJobRetentionUnit)));
    settingsPageInitialized_ = true;
}

void MainWindow::populateVerificationPage()
{
    const VerificationSnapshot verification = controller_->runtime().verification;
    verificationStatusLabel_->setText(QStringLiteral("Status: %1").arg(verificationStatusTitle(verification.state)));
    verificationDeviceIdLabel_->setText(QStringLiteral("Device ID: %1").arg(verification.deviceId.isEmpty() ? QStringLiteral("Unknown") : verification.deviceId));
    verificationEmojiList_->clear();
    for (const VerificationEmoji &emoji : verification.emojis) {
        verificationEmojiList_->addItem(QStringLiteral("%1  %2").arg(emoji.symbol, emoji.description));
    }
    if (verification.emojis.isEmpty()) {
        verificationEmojiList_->addItem(QStringLiteral("No SAS emoji available yet."));
    }

    QStringList decimalStrings;
    for (const quint16 value : verification.decimals) {
        decimalStrings.append(QString::number(value));
    }
    verificationDecimalsLabel_->setText(QStringLiteral("Decimals: %1").arg(decimalStrings.isEmpty() ? QStringLiteral("None") : decimalStrings.join(QLatin1Char(' '))));
}

void MainWindow::updateDetailLabels(
    QListWidget *listWidget,
    QLabel *titleLabel,
    QLabel *idLabel,
    QLabel *aliasLabel,
    QLabel *folderLabel,
    QLabel *watcherLabel,
    QLabel *historyLabel,
    QPlainTextEdit *aliasesTextEdit,
    const bool isSpace)
{
    const QListWidgetItem *item = listWidget->currentItem();
    if (item == nullptr) {
        titleLabel->setText(isSpace ? QStringLiteral("No Space Selected") : QStringLiteral("No Room Selected"));
        idLabel->clear();
        aliasLabel->clear();
        folderLabel->clear();
        watcherLabel->clear();
        historyLabel->clear();
        aliasesTextEdit->clear();
        return;
    }

    const QString roomId = item->data(Qt::UserRole).toString();
    RoomRecord selectedRoom;
    bool foundRoom = false;
    for (const RoomRecord &room : controller_->rooms()) {
        if (room.roomId == roomId) {
            selectedRoom = room;
            foundRoom = true;
            break;
        }
    }

    if (!foundRoom) {
        return;
    }

    titleLabel->setText(roomDisplayTitle(selectedRoom.roomId));
    idLabel->setText(QStringLiteral("Room ID: %1").arg(selectedRoom.roomId));
    aliasLabel->setText(QStringLiteral("Canonical Alias: %1").arg(selectedRoom.currentCanonicalAlias.isEmpty() ? QStringLiteral("None") : selectedRoom.currentCanonicalAlias));
    folderLabel->setText(QStringLiteral("Folder: %1").arg(selectedRoom.activeFolderLabel.isEmpty() ? QStringLiteral("None") : selectedRoom.activeFolderLabel));

    QString watcher = QStringLiteral("Not active");
    QString history = QStringLiteral("Idle");
    for (const RoomWorkerSnapshot &worker : controller_->runtime().workerStates) {
        if (worker.roomId == selectedRoom.roomId) {
            watcher = worker.liveWatcherActive ? QStringLiteral("Active") : QStringLiteral("Paused");
            history = roomHistoryModeTitle(worker.historyMode) + QStringLiteral(" · ") + worker.historyDetail;
            break;
        }
    }

    watcherLabel->setText(QStringLiteral("Live Watcher: %1").arg(watcher));
    historyLabel->setText(QStringLiteral("History Worker: %1").arg(history));

    const QStringList aliases = controller_->aliasHistory(selectedRoom.roomId);
    aliasesTextEdit->setPlainText(aliases.isEmpty() ? QStringLiteral("No aliases recorded yet.") : aliases.join(QLatin1Char('\n')));
}

QString MainWindow::roomDisplayTitle(const QString &roomId) const
{
    for (const RoomRecord &room : controller_->rooms()) {
        if (room.roomId == roomId) {
            if (!room.currentDisplayName.isEmpty()) {
                return room.currentDisplayName;
            }
            if (!room.currentCanonicalAlias.isEmpty()) {
                return room.currentCanonicalAlias;
            }
            return room.roomId;
        }
    }
    return roomId;
}

int MainWindow::workerSlotCount() const
{
    int highestWorkerId = 0;
    for (const ActiveDownloadSnapshot &download : controller_->runtime().activeDownloads) {
        highestWorkerId = qMax(highestWorkerId, download.workerId);
    }
    return qMax(qMax(controller_->settings().downloadWorkerCount, highestWorkerId), 1);
}

AppSettings MainWindow::gatherSettingsFromUi() const
{
    AppSettings settings = controller_->settings();
    settings.homeserverUrl = homeserverEdit_->text().trimmed();
    settings.username = usernameEdit_->text().trimmed();
    settings.ownerUserId = ownerIdEdit_->text().trimmed();
    settings.destinationRootPath = destinationEdit_->text().trimmed();
    settings.messageLimit = messageLimitSpin_->value();
    settings.timeWindowValue = timeWindowValueSpin_->value();
    settings.timeWindowUnit = static_cast<TimeWindowUnit>(timeWindowUnitCombo_->currentData().toInt());
    settings.retryCooldownMinutes = retryCooldownSpin_->value();
    settings.retryLimit = retryLimitSpin_->value();
    settings.downloadWorkerCount = downloadWorkersCombo_->currentData().toInt();
    settings.failedJobRetentionValue = failedRetentionValueSpin_->value();
    settings.failedJobRetentionUnit = static_cast<FailedJobRetentionUnit>(failedRetentionUnitCombo_->currentData().toInt());
    settings.desiredPowerState = powerToggle_->isChecked();
    return settings;
}

bool MainWindow::settingsInputsMatchController() const
{
    return settingsEqual(gatherSettingsFromUi(), controller_->settings())
        && passwordEdit_->text() == controller_->password();
}
