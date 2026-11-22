#ifndef GLOBALSHORTCUT_H
#define GLOBALSHORTCUT_H

#include <QObject>
#include <QVariantMap>
#include <functional>

#ifdef HAVE_DBUS
#include <QDBusArgument>
#include <QDBusObjectPath>
#endif

// A system-wide keyboard shortcut for the quick search, registered through
// the desktop's global shortcuts portal (KDE Plasma 6, GNOME 48+, Hyprland
// and others). The desktop asks the user to confirm it, and they can change
// it in the desktop's settings.
class GlobalShortcut : public QObject
{
    Q_OBJECT

public:
    explicit GlobalShortcut(QObject *parent = nullptr);
    ~GlobalShortcut() override;

    // Whether the desktop offers the portal
    static bool isSupported();

    // Registers the shortcut (the desktop may ask the user to confirm it).
    // With bindIfMissing false, only looks for one registered earlier.
    void enable(bool bindIfMissing = true);
    // Opens the desktop's settings to change the shortcut, where supported
    bool configure();

    // The key combination as the desktop describes it, if known
    QString triggerDescription() const { return m_trigger; }
    QString errorString() const { return m_error; }

signals:
    // The shortcut was pressed. The token lets the popup take focus on Wayland.
    void activated(const QString &activationToken);
    // The key combination or error changed
    void statusChanged();

#ifdef HAVE_DBUS
private slots:
    void onActivated(const QDBusObjectPath &session, const QString &shortcutId, qulonglong timestamp,
                     const QVariantMap &options);
    void onShortcutsChanged(const QDBusObjectPath &session, const QDBusArgument &shortcuts);
#endif

private:
    using Handler = std::function<void(uint response, const QVariantMap &results)>;
    void request(const QString &method, const QVariantList &arguments, Handler handler);
    void listOrBind();
    void bind();
    void readShortcuts(const QVariant &shortcuts);
    void fail(const QString &error);

    QString m_session;
    QString m_trigger;
    QString m_error;
    int m_requestCount = 0;
    bool m_bindIfMissing = true;
};

#endif // GLOBALSHORTCUT_H
