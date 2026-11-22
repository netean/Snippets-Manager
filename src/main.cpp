#include <QApplication>
#include <QCommandLineParser>
#include <QIcon>
#include "mainwindow.h"
#include "database.h"
#include "messagebox.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    app.setApplicationName("Snippet Manager");
    app.setApplicationVersion(APP_VERSION);
    app.setOrganizationName("SnippetManager");
    // Matches the installed desktop file, so desktops show the right name and icon
    app.setDesktopFileName(APP_ID);
    app.setWindowIcon(QIcon(":/icon.png"));

    // Supports --version and --help
    QCommandLineParser parser;
    parser.setApplicationDescription("Text and code snippet manager");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.process(app);

    QString dbError, dbWarning;
    if (!Database::instance().initialize(&dbError, &dbWarning)) {
        MessageBox::critical(nullptr, "Snippet Manager",
                             "The snippets database could not be opened, so Snippet Manager can't start.\n\n" + dbError);
        return 1;
    }

    MainWindow window;
    window.show();

    if (!dbWarning.isEmpty()) {
        MessageBox::warning(&window, "Snippet Manager", dbWarning);
    }
    QString upgradeNotice = Database::instance().takeUpgradeNotice();
    if (!upgradeNotice.isEmpty()) {
        MessageBox::information(&window, "Database Upgraded", upgradeNotice);
    }

    return app.exec();
}
