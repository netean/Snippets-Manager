#include "autostart.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>

namespace Autostart {

static QString executablePath()
{
    // Inside an AppImage the binary lives in a temporary mount; start the AppImage itself
    QString appImage = qEnvironmentVariable("APPIMAGE");
    return appImage.isEmpty() ? QCoreApplication::applicationFilePath() : appImage;
}

QString desktopExecQuote(const QString &path)
{
    static const QRegularExpression plain("^[A-Za-z0-9_./+-]+$");
    if (plain.match(path).hasMatch()) {
        return path;
    }
    // Inside quotes, " ` $ and \ are escaped; then \ is escaped again for the file
    QString escaped = path;
    escaped.replace("\\", "\\\\\\\\").replace("\"", "\\\\\"").replace("`", "\\\\`").replace("$", "\\\\$");
    return "\"" + escaped + "\"";
}

QString launchCommand(const QString &arguments)
{
    QString path = QDir::toNativeSeparators(executablePath());
    QString quoted = path.contains(' ') ? "\"" + path + "\"" : path;
    return arguments.isEmpty() ? quoted : quoted + " " + arguments;
}

#if defined(Q_OS_LINUX) || defined(Q_OS_FREEBSD)
static QString autostartFile()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
           + "/autostart/" + APP_ID + ".desktop";
}
#endif

bool isSupported()
{
#if defined(Q_OS_LINUX) || defined(Q_OS_FREEBSD) || defined(Q_OS_WIN)
    return true;
#else
    return false;
#endif
}

bool isEnabled()
{
#if defined(Q_OS_LINUX) || defined(Q_OS_FREEBSD)
    return QFile::exists(autostartFile());
#elif defined(Q_OS_WIN)
    QSettings run("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run", QSettings::NativeFormat);
    return run.contains("SnippetManager");
#else
    return false;
#endif
}

bool setEnabled(bool enabled, QString *errorMessage)
{
#if defined(Q_OS_LINUX) || defined(Q_OS_FREEBSD)
    QString path = autostartFile();
    if (!enabled) {
        return !QFile::exists(path) || QFile::remove(path);
    }
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (errorMessage) *errorMessage = file.errorString();
        return false;
    }
    file.write(QString("[Desktop Entry]\n"
                       "Type=Application\n"
                       "Name=Snippet Manager\n"
                       "Comment=Start Snippet Manager in the system tray\n"
                       "Exec=%1 --background\n"
                       "Icon=snippetmanager\n"
                       "Terminal=false\n"
                       "X-GNOME-Autostart-enabled=true\n")
                   .arg(desktopExecQuote(executablePath()))
                   .toUtf8());
    if (!file.commit()) {
        if (errorMessage) *errorMessage = file.errorString();
        return false;
    }
    return true;
#elif defined(Q_OS_WIN)
    QSettings run("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run", QSettings::NativeFormat);
    if (enabled) {
        run.setValue("SnippetManager", "\"" + QDir::toNativeSeparators(executablePath()) + "\" --background");
    } else {
        run.remove("SnippetManager");
    }
    return true;
#else
    Q_UNUSED(enabled)
    if (errorMessage) *errorMessage = "Starting at login isn't supported on this system.";
    return false;
#endif
}

} // namespace Autostart
