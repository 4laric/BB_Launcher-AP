// SPDX-FileCopyrightText: Copyright 2024 BBLauncher Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QMessageBox>
#include <QProgressBar>

#include "ModManager.h"
#include "ModMerger.h"
#include "modules/ui_ModManager.h"
#include "settings/config.h"

#ifdef Q_OS_WIN
#include <Windows.h>
#endif

#ifdef Q_OS_WIN
static bool IsNotNtfsDrive(const std::filesystem::path& folderPath) {
    std::filesystem::path absPath = std::filesystem::absolute(folderPath);
    std::wstring rootDrive = absPath.root_name().wstring() + L"\\";
    WCHAR fileSystemName[MAX_PATH + 1] = {0};
    BOOL success = GetVolumeInformationW(rootDrive.c_str(), NULL, 0, NULL, NULL, NULL,
                                         fileSystemName, MAX_PATH + 1);

    if (success) {
        std::wstring fsName(fileSystemName);
        if (fsName != L"NTFS") {
            return true;
        }
        return false;
    }

    // Default to false if unable to determine
    return false;
}
#endif

namespace {
modservice::OverlayMode DefaultOverlayMode() {
#if defined(FORCE_UAC) || !defined(_WIN32)
    return modservice::OverlayMode::Symlink;
#else
    return modservice::OverlayMode::Copy;
#endif
}

QString TransferLabel() {
#if defined(FORCE_UAC) || !defined(_WIN32)
    return QStringLiteral("Backing up original files, symlinking to shadPS4 mods folder");
#else
    return QStringLiteral("Backing up original files, copying to shadPS4 mods folder");
#endif
}
} // namespace

ModManager::ModManager(QWidget* parent) : QDialog(parent), ui(new Ui::ModManager) {
    ui->setupUi(this);
    ui->progressBar->setMinimum(0);
    ui->progressBar->setValue(0);
    this->setFixedSize(this->width(), this->height());

#if defined(Q_OS_WIN) && defined(FORCE_UAC)
    if (IsNotNtfsDrive(Common::ModPath)) {
        QMessageBox::information(this, "Drive Incompatibility Notice",
                                 "Mod Path is likely on an external drive or non-NTFS formatted "
                                 "drive. Activating mods may not work correctly due to incorrect "
                                 "symlink functions. You can try the BBLauncher noUAC version if "
                                 "you want to use the Mod Manager on non-NTFS drives");
    }

    if (IsNotNtfsDrive(Common::installPath)) {
        QMessageBox::information(
            this, "Drive Incompatibility Notice",
            "Bloodborne install folder is likely on an external drive or non-NTFS formatted "
            "drive. Activating mods may not work correctly due to incorrect "
            "symlink functions. You can try the BBLauncher noUAC version if "
            "you want to use the Mod Manager on non-NTFS drives");
    }
#endif

    if (Config::theme == "Dark") {
        ui->ActiveModList->setStyleSheet(
            "QListView::item { background-color: #000000; }"              // Color for odd rows
            "QListView::item:alternate { background-color: #242424; }"    // Color for even rows
            "QListWidget::item:selected { background-color: #AAB7DF; }"); // Color for selected row

        ui->InactiveModList->setStyleSheet(
            "QListView::item { background-color: #000000; }"
            "QListView::item:alternate { background-color: #242424; }"
            "QListWidget::item:selected { background-color: #AAB7DF; }");
    } else {
        ui->ActiveModList->setStyleSheet(
            "QListView::item { background-color: #ECECEC; }"
            "QListView::item:alternate { background-color: #D3D3D3; }"
            "QListWidget::item:selected { background-color: #AAB7DF; }");

        ui->InactiveModList->setStyleSheet(
            "QListView::item { background-color: #ECECEC; }"
            "QListView::item:alternate { background-color: #D3D3D3; }"
            "QListWidget::item:selected { background-color: #AAB7DF; }");
    }

    ui->ModHelpLabel->setText("<a "
                              "href=\"https://docs.google.com/document/d/"
                              "19ofjr6k4qqm9l9MJFrbDHEoNVyGXlK_8o95rI3xEh2k\">Click here for help "
                              "installing mods (especially BB Enhanced, Remaster, etc)</a>");
    ui->ModHelpLabel->setTextFormat(Qt::RichText);
    ui->ModHelpLabel->setTextInteractionFlags(Qt::TextBrowserInteraction);
    ui->ModHelpLabel->setOpenExternalLinks(true);

    std::filesystem::path installPath = Common::installPath;
    std::string filename;
    if (Core::FileSys::IsZArchiveFile(installPath)) {
        filename = Core::FileSys::StripZArchiveExtension(installPath).filename().string();
    } else {
        filename = installPath.filename().string();
    }

    if (Core::FileSys::IsZArchiveFile(installPath)) {
        installPath = installPath.parent_path() / filename;
    }

    ModInstallPath = installPath + "-mods";
    ModBackupPath = installPath.parent_path() / (filename + "-modsBACKUP");

    // The service alone owns activation state: active folders, overlay
    // files, conflict backups and the operation journal. The dialog only
    // owns selection, prompts and progress display.
    m_service = std::make_unique<modservice::ModService>(
        Common::ModPath, ModActivePath, ModInstallPath / "dvdroot_ps4", ModBackupPath,
        DefaultOverlayMode(), Common::GetBBLFilesPath() / "ModServiceJournal");

    RefreshLists();

    connect(ui->ResetButton, &QPushButton::pressed, this, &ModManager::ResetInstallation);
    connect(ui->ActivateButton, &QPushButton::pressed, this, &ModManager::ActivateMod);
    connect(ui->DeactivateButton, &QPushButton::pressed, this, &ModManager::DeactivateMod);
    connect(this, &ModManager::progressChanged, ui->progressBar, &QProgressBar::setValue);

    connect(ui->mergeButton, &QPushButton::pressed, this, [this]() {
        ModMerger* MergeWindow = new ModMerger(this);
        MergeWindow->exec();
        RefreshLists();
    });
}

void ModManager::ReportProgress(std::size_t done, std::size_t total) {
    ui->progressBar->setMaximum(static_cast<int>(total));
    emit progressChanged(static_cast<int>(done));
}

void ModManager::ActivateMod() {
    if (ui->InactiveModList->selectedItems().size() == 0) {
        QMessageBox::warning(
            this, "No mod selected",
            "Click on a mod in the Inactive Mods List before pressing Activate Mod");
        return;
    }

    const std::string ModName = ui->InactiveModList->currentItem()->text().toStdString();

    modservice::Plan plan;
    modservice::Result planned = m_service->PlanActivate(ModName, plan);
    if (!planned.ok && std::string(planned.code) == modservice::kConflict) {
        QString report;
        for (const std::string& entry : plan.conflictingMods) {
            report += QString::fromStdString(entry) + "\n";
        }
        if (QMessageBox::Yes ==
            QMessageBox::question(
                this, "Mod conflict found",
                report +
                    "\nThis file conflicts with the same file in this mod."
                    " Some conflicting mods cannot function properly "
                    "together.\n\nProceed with activation?",
                QMessageBox::Yes | QMessageBox::No)) {
            plan.conflictOverride = true;
        } else {
            return;
        }
    } else if (!planned.ok) {
        QMessageBox::warning(this, "Cannot activate mod",
                             QString::fromStdString(planned.detail));
        return;
    }

    ui->FileTransferLabel->setText(TransferLabel());
    ui->progressBar->setValue(0);

    modservice::Result result = m_service->Commit(
        plan, [this](std::size_t done, std::size_t total) { ReportProgress(done, total); });

    RefreshLists();
    ui->progressBar->setValue(0);
    ui->FileTransferLabel->setText("No Current File Transfers");

    if (!result.ok) {
        QMessageBox::information(this, "Error Activating Mod",
                                 "An error occurred activating mod " +
                                     QString::fromStdString(ModName) +
                                     ". Bloodbone may no longer function correctly. Resetting "
                                     "installation is recommendeded.\n\n" +
                                     QString::fromStdString(result.detail),
                                 QMessageBox::Ok);
    } else {
        QMessageBox::information(this, "Mod Activated",
                                 "Successfully activated mod " + QString::fromStdString(ModName),
                                 QMessageBox::Ok);
    }
}

void ModManager::DeactivateMod() {
    if (ui->ActiveModList->selectedItems().size() == 0) {
        QMessageBox::warning(
            this, "No mod selected",
            "Click on a mod in the Active Mods List before pressing Deactivate Mod");
        return;
    }

    const std::string ModName = ui->ActiveModList->currentItem()->text().toStdString();

    modservice::Plan plan;
    modservice::Result planned = m_service->PlanDeactivate(ModName, plan);
    if (!planned.ok) {
        if (std::string(planned.code) == modservice::kConflictOrder) {
            QMessageBox::warning(this, "Most recent conflicting mod must be uninstalled first",
                                 QString::fromStdString(planned.detail));
            ui->progressBar->setValue(0);
            ui->FileTransferLabel->setText("No Current File Transfers");
        } else {
            QMessageBox::warning(this, "Cannot deactivate mod",
                                 QString::fromStdString(planned.detail));
        }
        return;
    }

    const std::filesystem::path ModBackupFolderPath = ModBackupPath / ModName;
    const bool backupMissing = plan.backupMissing;
    if (backupMissing) {
        QMessageBox::warning(this,
                             "Unable to find Mod Backup " +
                                 QString::fromStdString(ModBackupFolderPath.string()),
                             "Unable to find Backup Folder, it may have been renamed or deleted.");
    }

    ui->progressBar->setValue(0);
    ui->FileTransferLabel->setText("Removing from shadPS4 mods Folder");

    modservice::Result result = m_service->Commit(
        plan, [this](std::size_t done, std::size_t total) { ReportProgress(done, total); });
    // Phase labels are coarse now that the service owns the file walk;
    // the bar still tracks real per-file progress above.
    ui->FileTransferLabel->setText("Reverting backup");

    RefreshLists();
    ui->progressBar->setValue(0);
    ui->FileTransferLabel->setText("No Current File Transfers");

    if (!result.ok || backupMissing) {
        QMessageBox::information(this, "Error Deactivating Mod",
                                 "An error occurred deactivating mod " +
                                     QString::fromStdString(ModName) +
                                     ". Bloodborne might not function correctly due to the error, "
                                     "resetting installation is recommended.\n\n" +
                                     QString::fromStdString(result.detail),
                                 QMessageBox::Ok);
    } else {
        QMessageBox::information(this, "Mod Deactivated",
                                 "Successfully deactivated mod " + QString::fromStdString(ModName),
                                 QMessageBox::Ok);
    }
}

void ModManager::RefreshLists() {
    QStringList ActiveModStringList;
    QStringList InactiveModStringList;

    ui->ActiveModList->clear();
    ui->InactiveModList->clear();

    for (const std::string& FolderName : m_service->ActiveMods()) {
        ActiveModStringList.append(QString::fromStdString(FolderName));
    }
    ActiveModStringList.sort(Qt::CaseInsensitive);
    ui->ActiveModList->addItems(ActiveModStringList);

    for (const std::string& FolderName : m_service->InactiveMods()) {
        InactiveModStringList.append(QString::fromStdString(FolderName));
    }
    InactiveModStringList.sort(Qt::CaseInsensitive);
    ui->InactiveModList->addItems(InactiveModStringList);
}

void ModManager::ResetInstallation() {
    if (QMessageBox::No == QMessageBox::question(this, "Reset Installation",
                                                 "This will deactivate all mods (original files "
                                                 "remain untouched).\n\nProceed with reset?",
                                                 QMessageBox::Yes | QMessageBox::No)) {
        return;
    }

    modservice::Result result = m_service->ResetInstallation();

    RefreshLists();
    if (!result.ok) {
        QMessageBox::warning(this, "Filesystem error",
                             "Error resetting installation. Make sure all game and mod "
                             "folders/files are not open or in use\n\n" +
                                 QString::fromStdString(result.detail));
        return;
    }

    QMessageBox::information(this, "Reset Complete", "Reset Successfully completed",
                             QMessageBox::Ok);
}

ModManager::~ModManager() {
    delete ui;
}
