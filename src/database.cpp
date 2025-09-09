#include "database.h"
#include <QSqlError>
#include <QStandardPaths>
#include <QDir>
#include <QDebug>

Database& Database::instance()
{
    static Database instance;
    return instance;
}

bool Database::initialize()
{
    // Create data directory if it doesn't exist
    QString dataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dataPath);
    
    m_db = QSqlDatabase::addDatabase("QSQLITE");
    m_db.setDatabaseName(dataPath + "/snippets.db");
    
    if (!m_db.open()) {
        qDebug() << "Failed to open database:" << m_db.lastError().text();
        return false;
    }
    
    return createTables();
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

QList<Snippet> Database::getAllSnippets()
{
    QList<Snippet> snippets;
    QSqlQuery query("SELECT id, title, content, created, modified FROM snippets ORDER BY modified DESC", m_db);
    
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

QList<Snippet> Database::searchSnippets(const QString& searchTerm)
{
    QList<Snippet> snippets;
    QSqlQuery query(m_db);
    query.prepare("SELECT id, title, content, created, modified FROM snippets WHERE title LIKE ? OR content LIKE ? ORDER BY modified DESC");
    QString term = "%" + searchTerm + "%";
    query.addBindValue(term);
    query.addBindValue(term);
    
    if (query.exec()) {
        while (query.next()) {
            Snippet snippet;
            snippet.id = query.value(0).toInt();
            snippet.title = query.value(1).toString();
            snippet.content = query.value(2).toString();
            snippet.created = query.value(3).toDateTime();
            snippet.modified = query.value(4).toDateTime();
            snippets.append(snippet);
        }
    }
    
    return snippets;
}

bool Database::addSnippet(const QString& title, const QString& content)
{
    QSqlQuery query(m_db);
    query.prepare("INSERT INTO snippets (title, content) VALUES (?, ?)");
    query.addBindValue(title);
    query.addBindValue(content);
    
    return query.exec();
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