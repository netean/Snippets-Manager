#include "database.h"
#include <QSqlError>
#include <QStandardPaths>
#include <QSettings>
#include <QFileInfo>
#include <QFile>
#include <QDir>
#include <QDebug>

static const char *DatabasePathKey = "database/path";

Database& Database::instance()
{
    static Database instance;
    return instance;
}

QString Database::defaultDatabasePath()
{
    QString dataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return dataPath + "/snippets.db";
}

QString Database::databasePath() const
{
    return m_db.databaseName();
}

bool Database::initialize()
{
    QSettings settings;
    QString path = settings.value(DatabasePathKey, defaultDatabasePath()).toString();

    QString error;
    if (openAt(path, &error)) {
        return true;
    }

    qWarning() << "Failed to open database" << path << ":" << error;

    // Fall back to the default location if a custom database can't be opened
    // (e.g. it lives on a drive that isn't mounted). The saved setting is kept
    // so the custom database is tried again on the next start.
    if (QFileInfo(path).absoluteFilePath() != QFileInfo(defaultDatabasePath()).absoluteFilePath()) {
        if (openAt(defaultDatabasePath(), &error)) {
            return true;
        }
        qWarning() << "Failed to open default database:" << error;
    }

    return false;
}

bool Database::openAt(const QString& path, QString* errorMessage)
{
    if (!m_db.isValid()) {
        m_db = QSqlDatabase::addDatabase("QSQLITE");
    }
    if (m_db.isOpen()) {
        m_db.close();
    }

    QString absolutePath = QFileInfo(path).absoluteFilePath();
    QDir().mkpath(QFileInfo(absolutePath).absolutePath());
    m_db.setDatabaseName(absolutePath);

    if (!m_db.open()) {
        if (errorMessage) *errorMessage = m_db.lastError().text();
        return false;
    }

    // SQLite opens lazily, so creating the table is also what tells us
    // whether the file is actually a usable SQLite database.
    if (!createTables()) {
        if (errorMessage) *errorMessage = QString("'%1' is not a valid snippets database.").arg(absolutePath);
        m_db.close();
        return false;
    }

    return true;
}

bool Database::openDatabase(const QString& path, QString* errorMessage)
{
    if (!QFileInfo::exists(path)) {
        if (errorMessage) *errorMessage = QString("'%1' does not exist.").arg(path);
        return false;
    }

    QString previousPath = databasePath();
    if (!openAt(path, errorMessage)) {
        if (!previousPath.isEmpty()) {
            openAt(previousPath, nullptr);
        }
        return false;
    }

    QSettings().setValue(DatabasePathKey, databasePath());
    return true;
}

bool Database::createDatabase(const QString& path, QString* errorMessage)
{
    QString absolutePath = QFileInfo(path).absoluteFilePath();
    if (absolutePath == databasePath()) {
        if (errorMessage) *errorMessage = "That file is the database currently in use.";
        return false;
    }

    // The caller has already confirmed replacing an existing file
    if (QFileInfo::exists(absolutePath) && !QFile::remove(absolutePath)) {
        if (errorMessage) *errorMessage = QString("Could not replace '%1'.").arg(absolutePath);
        return false;
    }

    QString previousPath = databasePath();
    if (!openAt(absolutePath, errorMessage)) {
        if (!previousPath.isEmpty()) {
            openAt(previousPath, nullptr);
        }
        return false;
    }

    QSettings().setValue(DatabasePathKey, databasePath());
    return true;
}

bool Database::moveDatabase(const QString& newPath, QString* errorMessage)
{
    QString oldPath = databasePath();
    QString absoluteNewPath = QFileInfo(newPath).absoluteFilePath();
    if (absoluteNewPath == oldPath) {
        return true;
    }

    m_db.close();

    QDir().mkpath(QFileInfo(absoluteNewPath).absolutePath());

    // The caller has already confirmed replacing an existing file
    if (QFileInfo::exists(absoluteNewPath) && !QFile::remove(absoluteNewPath)) {
        if (errorMessage) *errorMessage = QString("Could not replace '%1'.").arg(absoluteNewPath);
        openAt(oldPath, nullptr);
        return false;
    }

    if (!QFile::copy(oldPath, absoluteNewPath)) {
        if (errorMessage) *errorMessage = QString("Could not copy the database to '%1'.").arg(absoluteNewPath);
        openAt(oldPath, nullptr);
        return false;
    }

    if (!openAt(absoluteNewPath, errorMessage)) {
        QFile::remove(absoluteNewPath);
        openAt(oldPath, nullptr);
        return false;
    }

    QSettings().setValue(DatabasePathKey, databasePath());

    if (!QFile::remove(oldPath)) {
        qWarning() << "Database moved, but the old file could not be removed:" << oldPath;
    }

    return true;
}

bool Database::createTables()
{
    QSqlQuery query(m_db);
    QString createTable = R"(
        CREATE TABLE IF NOT EXISTS snippets (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            title TEXT NOT NULL,
            content TEXT NOT NULL,
            created DATETIME DEFAULT CURRENT_TIMESTAMP,
            modified DATETIME DEFAULT CURRENT_TIMESTAMP
        )
    )";
    
    if (!query.exec(createTable)) {
        qDebug() << "Failed to create table:" << query.lastError().text();
        return false;
    }
    
    return true;
}

QString Database::orderByClause(SortOrder order)
{
    // id breaks ties so snippets created in the same second keep a stable order
    switch (order) {
    case TitleAscending:
        return "ORDER BY title COLLATE NOCASE ASC, id ASC";
    case TitleDescending:
        return "ORDER BY title COLLATE NOCASE DESC, id DESC";
    case CreatedAscending:
        return "ORDER BY created ASC, id ASC";
    case CreatedDescending:
    default:
        return "ORDER BY created DESC, id DESC";
    }
}

QList<Snippet> Database::readSnippets(QSqlQuery& query)
{
    QList<Snippet> snippets;
    while (query.next()) {
        Snippet snippet;
        snippet.id = query.value(0).toInt();
        snippet.title = query.value(1).toString();
        snippet.content = query.value(2).toString();
        snippet.created = query.value(3).toDateTime();
        snippet.modified = query.value(4).toDateTime();
        snippets.append(snippet);
    }
    return snippets;
}

QList<Snippet> Database::getAllSnippets(SortOrder order)
{
    QSqlQuery query(m_db);
    if (!query.exec("SELECT id, title, content, created, modified FROM snippets " + orderByClause(order))) {
        return {};
    }
    return readSnippets(query);
}

QList<Snippet> Database::searchSnippets(const QString& searchTerm, SortOrder order)
{
    QSqlQuery query(m_db);
    query.prepare("SELECT id, title, content, created, modified FROM snippets WHERE title LIKE ? OR content LIKE ? "
                  + orderByClause(order));
    QString term = "%" + searchTerm + "%";
    query.addBindValue(term);
    query.addBindValue(term);
    
    if (!query.exec()) {
        return {};
    }
    return readSnippets(query);
}

int Database::addSnippet(const QString& title, const QString& content)
{
    QSqlQuery query(m_db);
    query.prepare("INSERT INTO snippets (title, content) VALUES (?, ?)");
    query.addBindValue(title);
    query.addBindValue(content);
    
    if (!query.exec()) {
        return -1;
    }
    return query.lastInsertId().toInt();
}

bool Database::updateSnippet(int id, const QString& title, const QString& content)
{
    QSqlQuery query(m_db);
    query.prepare("UPDATE snippets SET title = ?, content = ?, modified = CURRENT_TIMESTAMP WHERE id = ?");
    query.addBindValue(title);
    query.addBindValue(content);
    query.addBindValue(id);
    
    return query.exec();
}

bool Database::deleteSnippet(int id)
{
    QSqlQuery query(m_db);
    query.prepare("DELETE FROM snippets WHERE id = ?");
    query.addBindValue(id);
    
    return query.exec();
}

Snippet Database::getSnippet(int id)
{
    Snippet snippet;
    QSqlQuery query(m_db);
    query.prepare("SELECT id, title, content, created, modified FROM snippets WHERE id = ?");
    query.addBindValue(id);
    
    if (query.exec() && query.next()) {
        snippet.id = query.value(0).toInt();
        snippet.title = query.value(1).toString();
        snippet.content = query.value(2).toString();
        snippet.created = query.value(3).toDateTime();
        snippet.modified = query.value(4).toDateTime();
    }
    
    return snippet;
}
