// SPDX-FileCopyrightText: Copyright 2024 BBLauncher Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <filesystem>
#include <memory>
#include <QDialog>

#include "Common.h"
#include "ModService.h"

namespace Ui {
class ModManager;
}

class ModManager : public QDialog {
    Q_OBJECT

public:
    explicit ModManager(QWidget* parent = nullptr);
    ~ModManager();

signals:
    void progressChanged(int value);

private slots:
    void ActivateMod();
    void DeactivateMod();

private:
    Ui::ModManager* ui;

    void RefreshLists();
    void ReportProgress(std::size_t done, std::size_t total);
    void ResetInstallation();

    std::unique_ptr<modservice::ModService> m_service;

    std::filesystem::path ModInstallPath;
    std::filesystem::path ModBackupPath;
    const std::filesystem::path ModActivePath =
        Common::GetBBLFilesPath() / "Mods-Active (DO NOT DELETE)";
};
