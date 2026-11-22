#ifndef SINGLEINSTANCE_H
#define SINGLEINSTANCE_H

#include <QObject>

class QLocalServer;

// Lets a second launch of the app pass a command (e.g. "popup" from
// `snippetmanager --popup`) to the copy that's already running, instead of
// starting another one
class SingleInstance : public QObject
{
    Q_OBJECT

public:
    explicit SingleInstance(QObject *parent = nullptr);

    // Sends a command to the running copy; returns false if there isn't one
    static bool sendToRunning(const QString &command);

    // Starts listening for commands from later launches
    bool listen();

signals:
    void commandReceived(const QString &command);

private:
    static QString serverName();
    QLocalServer *m_server = nullptr;
};

#endif // SINGLEINSTANCE_H
