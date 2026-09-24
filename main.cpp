// SPDX-FileCopyrightText: Copyright 2024 BBLauncher Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QApplication>
#include <QCommandLineParser>
#include <QIcon>
#include <QMessageBox>

#if defined(_MSC_VER)
#include <intrin.h>
#endif

#ifndef USE_WEBENGINE
#include <QtWebView>
#include <QQuickView>
#include <QQuickItem>
#endif

#include "modules/RunGuard.h"
#include "modules/ApFork.h"
#include "modules/bblauncher.h"

void customMessageHandler(QtMsgType, const QMessageLogContext&, const QString&) {}

int main(int argc, char* argv[]) {
#ifdef Q_OS_MAC
    qputenv("QT_WEBVIEW_PLUGIN", "native");
#endif

    QApplication a(argc, argv);
    QApplication::setStyle("Fusion");
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/BBicon2.ico")));

    QCommandLineParser parser;
    QCommandLineOption noGui("n");
    parser.addOption(noGui);
    QCommandLineOption apSeed("ap-seed", "Headless Archipelago Play with this seed file.", "path");
    QCommandLineOption apPlayer("ap-player", "AP player name (multi-slot seeds).", "name");
    QCommandLineOption apServer("ap-server", "AP server address (when the seed omits it).",
                                "address");
    parser.addOption(apSeed);
    parser.addOption(apPlayer);
    parser.addOption(apServer);
#ifdef BB_AP_FORK
    QCommandLineOption apPackageSmoke(
        "ap-package-smoke", "Verify the packaged launcher and Qt runtime can initialize.");
    parser.addOption(apPackageSmoke);
    QCommandLineOption apWebViewSmoke(
        "ap-webview-smoke", "Verify the packaged Mod Downloader browser loads without opening a site.");
    parser.addOption(apWebViewSmoke);
#endif
    parser.process(a);
#ifdef BB_AP_FORK
    // Packaging smoke runs before RunGuard, application settings, and any
    // window construction. Reaching this point proves the executable and
    // its Qt runtime loaded without touching a user installation.
    if (parser.isSet(apPackageSmoke)) {
        return 0;
    }
#endif

#ifndef USE_WEBENGINE
    QtWebView::initialize();
#ifdef BB_AP_FORK
    if (parser.isSet(apWebViewSmoke)) {
        QQuickView view;
        view.setSource(QUrl(QStringLiteral("qrc:/web.qml")));
        auto* root = view.rootObject();
        return root != nullptr && root->findChild<QObject*>(QStringLiteral("currentWebView")) != nullptr
                   ? 0 : 2;
    }
#endif
#else
#ifdef BB_AP_FORK
    if (parser.isSet(apWebViewSmoke)) return 0;
#endif
#endif
    bool noGUIset = parser.isSet(noGui);

#ifdef BB_AP_FORK
    RunGuard guard(QStringLiteral("BBLauncher-AP-single-instance"));
#else
    RunGuard guard("d8976skj86874hkj287960980lkjhfka1#Q$^&*");
#endif
    bool noinstancerunning = guard.tryToRun();

    BBLauncher* main_window = new BBLauncher(noGUIset, noinstancerunning, nullptr);

    bool hasInstance = false;
    if (!noinstancerunning) {
        QMessageBox::warning(nullptr, "BB_Launcher already running",
                             "Only one instance of BB_Launcher can run at a time");
        main_window->close();
        return 0;
    }

    noGUIset ? main_window->hide() : main_window->show();

    if (!main_window->canLaunch) {
        return 0;
    }

    // Keep this launcher process alive as the explicit owner of the backend
    // and native AP client. No detached supervisor survives its lifetime.
    if (parser.isSet(apSeed)) {
        const int code = main_window->RunApHeadless(parser.value(apSeed),
                                                    parser.value(apPlayer),
                                                    parser.value(apServer));
        if (code != 0) {
            return code;
        }
        return a.exec();
    }

    return a.exec();
}
