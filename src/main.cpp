#include <QApplication>
#include <QDir>
#include <QStandardPaths>
#include <QIcon>
#include <QDebug>
#include "mainwindow.h"
#include "database.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    
    app.setApplicationName("Snippet Manager");
    app.setApplicationVersion("1.0");
    app.setOrganizationName("SnippetManager");
    
    // Set application icon with multiple fallbacks
    QIcon appIcon;
    
    // Try PNG resource first (best for most platforms)
    if (!QIcon(":/icon.png").isNull()) {
        appIcon = QIcon(":/icon.png");
        qDebug() << "Using resource PNG icon";
    }
    // Try SVG resource (scalable)
    else if (!QIcon(":/icon.svg").isNull()) {
        appIcon = QIcon(":/icon.svg");
        qDebug() << "Using resource SVG icon";
    }
    // Try system theme icon as fallback
    else {
        appIcon = QIcon::fromTheme("text-x-generic", QIcon(":/icon.png"));
        qDebug() << "Using system theme icon fallback";
    }
    
    if (!appIcon.isNull()) {
        app.setWindowIcon(appIcon);
        qDebug() << "Application icon set successfully";
    } else {
        qDebug() << "Warning: No application icon could be loaded";
    }
    
    // Initialize database
    Database::instance().initialize();
    
    MainWindow window;
    window.show();
    
    return app.exec();
}