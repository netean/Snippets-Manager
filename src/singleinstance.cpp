#include "singleinstance.h"
#include <QDir>
#include <QLocalServer>
#include <QLocalSocket>
#include <QStandardPaths>

SingleInstance::SingleInstance(QObject *parent)
    : QObject(parent)
{
}

QString SingleInstance::serverName()
{
#ifdef Q_OS_WIN
    return QString(APP_ID) + "-" + qEnvironmentVariable("USERNAME");
#else
    // The runtime directory is private to the user, so only they can send commands
    QString dir = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (dir.isEmpty()) {
        dir = QDir::tempPath();
    }
    QString path = dir + "/" + APP_ID + ".socket";
    // Unix socket addresses are limited to about 100 bytes. If the runtime
    // folder's path is too long, use a short name (Qt puts it in the
    // temporary folder) that's still unique to this runtime folder.
    if (path.toUtf8().size() >= 100) {
        return QString("snippetmanager-%1").arg(qHash(dir), 0, 16);
    }
    return path;
#endif
}

bool SingleInstance::sendToRunning(const QString &command)
{
    QLocalSocket socket;
    socket.connectToServer(serverName());
    if (!socket.waitForConnected(500)) {
        return false;
    }
    socket.write(command.toUtf8() + "\n");
    socket.waitForBytesWritten(1000);
    socket.disconnectFromServer();
    return true;
}

bool SingleInstance::listen()
{
    m_server = new QLocalServer(this);
    m_server->setSocketOptions(QLocalServer::UserAccessOption);
    // A previous run that crashed can leave its socket behind
    QLocalServer::removeServer(serverName());
    if (!m_server->listen(serverName())) {
        qWarning() << "Couldn't listen for other launches:" << m_server->errorString();
        return false;
    }
    connect(m_server, &QLocalServer::newConnection, this, [this]() {
        while (QLocalSocket *socket = m_server->nextPendingConnection()) {
            connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
            connect(socket, &QLocalSocket::readyRead, this, [this, socket]() {
                while (socket->canReadLine()) {
                    // Commands are short; ignore anything that isn't
                    QString command = QString::fromUtf8(socket->readLine(1024)).trimmed();
                    if (!command.isEmpty()) {
                        emit commandReceived(command);
                    }
                }
            });
        }
    });
    return true;
}
