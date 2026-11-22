#ifndef TESTENV_H
#define TESTENV_H

#include <QCoreApplication>
#include <QSettings>

// On Windows and macOS, QSettings reads and writes nothing until the app has
// an organisation name (the real app sets one in main). Settings go in an
// INI file, so tests never touch the Windows registry.
static void setUpTestEnvironment()
{
    QCoreApplication::setOrganizationName("SnippetManagerTests");
    QSettings::setDefaultFormat(QSettings::IniFormat);
}
Q_COREAPP_STARTUP_FUNCTION(setUpTestEnvironment)

#endif // TESTENV_H
