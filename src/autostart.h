#ifndef AUTOSTART_H
#define AUTOSTART_H

#include <QString>

// Starting the app (in the tray) when the user logs in
namespace Autostart {

bool isSupported();
bool isEnabled();
bool setEnabled(bool enabled, QString *errorMessage = nullptr);

// The command that starts this copy of the app (the AppImage, if run from one)
QString launchCommand(const QString &arguments);

// Quotes a command for an Exec= line in a desktop file
QString desktopExecQuote(const QString &path);

} // namespace Autostart

#endif // AUTOSTART_H
