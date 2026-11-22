#include <QApplication>
#include <QCommandLineParser>
#include <QIcon>
#include "database.h"
#include "mainwindow.h"
#include "messagebox.h"
#include "singleinstance.h"

int main(int argc, char *argv[])
{
    // Read before Qt starts: on Wayland, this token (from the launcher or
    // shortcut that started us) lets the window we open take the focus
    QString activationToken = qEnvironmentVariable("XDG_ACTIVATION_TOKEN");

    QApplication app(argc, argv);

    app.setApplicationName("Snippet Manager");
    app.setApplicationVersion(APP_VERSION);
    app.setOrganizationName("SnippetManager");
    // Matches the installed desktop file, so desktops show the right name and icon
    app.setDesktopFileName(APP_ID);
    app.setWindowIcon(QIcon(":/icon.png"));

    QCommandLineParser parser;
    parser.setApplicationDescription("Text and code snippet manager");
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption popupOption("popup", "Open the quick search (in the copy that's already running, if there is one).");
    QCommandLineOption backgroundOption("background", "Start in the system tray without opening the window.");
    parser.addOption(popupOption);
    parser.addOption(backgroundOption);
    parser.process(app);

    // If Snippet Manager is already running, pass the request to it instead
    QString command = parser.isSet(popupOption) ? "popup " + activationToken
                      : parser.isSet(backgroundOption) ? QString() : "show";
    if (SingleInstance::sendToRunning(command.isEmpty() ? "ping" : command)) {
        return 0;
    }
    SingleInstance instance;
    instance.listen();

    QString dbError, dbWarning;
    if (!Database::instance().initialize(&dbError, &dbWarning)) {
        MessageBox::critical(nullptr, "Snippet Manager",
                             "The snippets database could not be opened, so Snippet Manager can't start.\n\n" + dbError);
        return 1;
    }

    MainWindow window;
    QObject::connect(&instance, &SingleInstance::commandReceived, &window, &MainWindow::handleCommand);

    // Without a tray to live in, there must be a window
    bool background = (parser.isSet(backgroundOption) || parser.isSet(popupOption)) && window.canRunInBackground();
    if (!background) {
        window.show();
    }
    if (parser.isSet(popupOption)) {
        window.handleCommand("popup " + activationToken);
    }

    if (!dbWarning.isEmpty()) {
        MessageBox::warning(background ? nullptr : &window, "Snippet Manager", dbWarning);
    }
    QString upgradeNotice = Database::instance().takeUpgradeNotice();
    if (!upgradeNotice.isEmpty()) {
        MessageBox::information(background ? nullptr : &window, "Database Upgraded", upgradeNotice);
    }

    return app.exec();
}
