#include "globalshortcut.h"

#ifdef HAVE_DBUS

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#include <QRandomGenerator>

static const char *PortalService = "org.freedesktop.portal.Desktop";
static const char *PortalPath = "/org/freedesktop/portal/desktop";
static const char *ShortcutsInterface = "org.freedesktop.portal.GlobalShortcuts";
static const char *QuickSearchId = "quick-search";
// Meta+Alt+V: desktop-level shortcuts use Meta, and V suggests paste
static const char *PreferredTrigger = "LOGO+ALT+v";

// a(sa{sv}): the shortcuts list the portal sends and receives
struct PortalShortcut {
    QString id;
    QVariantMap properties;
};
Q_DECLARE_METATYPE(PortalShortcut)
using PortalShortcutList = QList<PortalShortcut>;
Q_DECLARE_METATYPE(PortalShortcutList)

QDBusArgument &operator<<(QDBusArgument &argument, const PortalShortcut &shortcut)
{
    argument.beginStructure();
    argument << shortcut.id << shortcut.properties;
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, PortalShortcut &shortcut)
{
    argument.beginStructure();
    argument >> shortcut.id >> shortcut.properties;
    argument.endStructure();
    return argument;
}

// Receives the Response signal of one portal request
class PortalRequest : public QObject
{
    Q_OBJECT
public:
    explicit PortalRequest(std::function<void(uint, const QVariantMap &)> handler, QObject *parent)
        : QObject(parent), m_handler(std::move(handler)) {}
public slots:
    void onResponse(uint response, const QVariantMap &results)
    {
        m_handler(response, results);
        deleteLater();
    }
private:
    std::function<void(uint, const QVariantMap &)> m_handler;
};

GlobalShortcut::GlobalShortcut(QObject *parent)
    : QObject(parent)
{
    qDBusRegisterMetaType<PortalShortcut>();
    qDBusRegisterMetaType<PortalShortcutList>();
}

GlobalShortcut::~GlobalShortcut()
{
    if (!m_session.isEmpty()) {
        QDBusMessage close = QDBusMessage::createMethodCall(PortalService, m_session,
                                                            "org.freedesktop.portal.Session", "Close");
        QDBusConnection::sessionBus().call(close, QDBus::NoBlock);
    }
}

bool GlobalShortcut::isSupported()
{
    QDBusMessage get = QDBusMessage::createMethodCall(PortalService, PortalPath,
                                                      "org.freedesktop.DBus.Properties", "Get");
    get << QString(ShortcutsInterface) << QString("version");
    QDBusMessage reply = QDBusConnection::sessionBus().call(get, QDBus::Block, 2000);
    return reply.type() == QDBusMessage::ReplyMessage;
}

void GlobalShortcut::fail(const QString &error)
{
    qWarning() << "Global shortcut:" << error;
    m_error = error;
    emit statusChanged();
}

void GlobalShortcut::request(const QString &method, const QVariantList &arguments, Handler handler)
{
    // The portal answers with a Response signal on a request object whose path
    // is predictable from our bus name and a token. Subscribe before calling,
    // so the answer can't arrive first.
    QDBusConnection bus = QDBusConnection::sessionBus();
    QString token = QString("snippetmanager%1_%2").arg(++m_requestCount).arg(QRandomGenerator::global()->generate());
    QString sender = bus.baseService().mid(1).replace('.', '_');
    QString requestPath = QString("/org/freedesktop/portal/desktop/request/%1/%2").arg(sender, token);

    auto *pending = new PortalRequest(std::move(handler), this);
    bus.connect(PortalService, requestPath, "org.freedesktop.portal.Request", "Response",
                pending, SLOT(onResponse(uint,QVariantMap)));

    QDBusMessage call = QDBusMessage::createMethodCall(PortalService, PortalPath, ShortcutsInterface, method);
    QVariantList args = arguments;
    // The options map is always the last argument
    QVariantMap options = args.isEmpty() ? QVariantMap() : args.takeLast().toMap();
    options["handle_token"] = token;
    args << options;
    call.setArguments(args);

    auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, pending, method](QDBusPendingCallWatcher *w) {
        if (w->isError()) {
            pending->deleteLater();
            fail(QString("%1 failed: %2").arg(method, w->error().message()));
        }
        w->deleteLater();
    });
}

void GlobalShortcut::enable(bool bindIfMissing)
{
    m_bindIfMissing = bindIfMissing;
    m_error.clear();
    QDBusConnection bus = QDBusConnection::sessionBus();

    // Tell the portal which app this is (needed for apps that aren't sandboxed);
    // older portals don't have this and don't need it
    QDBusMessage registration = QDBusMessage::createMethodCall(PortalService, PortalPath,
                                                               "org.freedesktop.host.portal.Registry", "Register");
    registration << QString(APP_ID) << QVariantMap();
    bus.call(registration, QDBus::Block, 2000);

    bus.connect(PortalService, PortalPath, ShortcutsInterface, "Activated",
                this, SLOT(onActivated(QDBusObjectPath,QString,qulonglong,QVariantMap)));
    bus.connect(PortalService, PortalPath, ShortcutsInterface, "ShortcutsChanged",
                this, SLOT(onShortcutsChanged(QDBusObjectPath,QDBusArgument)));

    QVariantMap options;
    options["session_handle_token"] = QString("snippetmanager_session");
    request("CreateSession", {options}, [this](uint response, const QVariantMap &results) {
        if (response != 0) {
            fail("The desktop didn't allow a shortcut session.");
            return;
        }
        QVariant handle = results.value("session_handle");
        m_session = handle.canConvert<QDBusObjectPath>() ? handle.value<QDBusObjectPath>().path() : handle.toString();
        listOrBind();
    });
}

void GlobalShortcut::listOrBind()
{
    // If the desktop already knows the shortcut (from an earlier run), don't ask again
    request("ListShortcuts", {QVariant::fromValue(QDBusObjectPath(m_session)), QVariantMap()},
            [this](uint response, const QVariantMap &results) {
                if (response == 0) {
                    readShortcuts(results.value("shortcuts"));
                }
                if (m_trigger.isEmpty() && m_bindIfMissing) {
                    bind();
                } else if (m_trigger.isEmpty()) {
                    emit statusChanged();
                }
            });
}

void GlobalShortcut::bind()
{
    PortalShortcut shortcut;
    shortcut.id = QuickSearchId;
    shortcut.properties["description"] = QString("Open Snippet Manager's quick search");
    shortcut.properties["preferred_trigger"] = QString(PreferredTrigger);
    PortalShortcutList shortcuts{shortcut};

    request("BindShortcuts",
            {QVariant::fromValue(QDBusObjectPath(m_session)), QVariant::fromValue(shortcuts), QString(), QVariantMap()},
            [this](uint response, const QVariantMap &results) {
                if (response == 1) {
                    fail("The shortcut wasn't set up (cancelled).");
                    return;
                }
                if (response != 0) {
                    fail("The desktop couldn't set up the shortcut.");
                    return;
                }
                readShortcuts(results.value("shortcuts"));
            });
}

void GlobalShortcut::readShortcuts(const QVariant &shortcuts)
{
    if (!shortcuts.canConvert<QDBusArgument>()) return;
    PortalShortcutList list = qdbus_cast<PortalShortcutList>(shortcuts.value<QDBusArgument>());
    for (const PortalShortcut &shortcut : list) {
        if (shortcut.id == QuickSearchId) {
            m_trigger = shortcut.properties.value("trigger_description").toString();
            if (m_trigger.isEmpty()) {
                m_trigger = "Set (no keys assigned yet)";
            }
            m_error.clear();
            emit statusChanged();
        }
    }
}

bool GlobalShortcut::configure()
{
    if (m_session.isEmpty()) return false;
    // ConfigureShortcuts (portal version 2) opens the desktop's settings for it
    QDBusMessage call = QDBusMessage::createMethodCall(PortalService, PortalPath, ShortcutsInterface, "ConfigureShortcuts");
    call << QVariant::fromValue(QDBusObjectPath(m_session)) << QString() << QVariantMap();
    QDBusMessage reply = QDBusConnection::sessionBus().call(call, QDBus::Block, 5000);
    return reply.type() == QDBusMessage::ReplyMessage;
}

void GlobalShortcut::onActivated(const QDBusObjectPath &session, const QString &shortcutId, qulonglong timestamp,
                                 const QVariantMap &options)
{
    Q_UNUSED(timestamp)
    if (session.path() != m_session || shortcutId != QuickSearchId) return;
    emit activated(options.value("activation_token").toString());
}

void GlobalShortcut::onShortcutsChanged(const QDBusObjectPath &session, const QDBusArgument &shortcuts)
{
    if (session.path() != m_session) return;
    PortalShortcutList list = qdbus_cast<PortalShortcutList>(shortcuts);
    for (const PortalShortcut &shortcut : list) {
        if (shortcut.id == QuickSearchId) {
            m_trigger = shortcut.properties.value("trigger_description").toString();
            emit statusChanged();
        }
    }
}

#include "globalshortcut.moc"

#else  // No D-Bus: global shortcuts aren't available

GlobalShortcut::GlobalShortcut(QObject *parent) : QObject(parent) {}
GlobalShortcut::~GlobalShortcut() = default;
bool GlobalShortcut::isSupported() { return false; }
void GlobalShortcut::enable(bool) { m_error = "Global shortcuts aren't supported on this system."; emit statusChanged(); }
bool GlobalShortcut::configure() { return false; }

#endif
