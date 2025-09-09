#include <QApplication>
#include <QDir>
#include <QStandardPaths>
#include "mainwindow.h"
#include "database.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    
    app.setApplicationName("Snippet Manager");
    app.setApplicationVersion("1.0");
    app.setOrganizationName("SnippetManager");
    
    // Initialize database
    Database::instance().initialize();
    
    MainWindow window;
    window.show();
    
    return app.exec();
}