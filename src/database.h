#ifndef DATABASE_H
#define DATABASE_H

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QString>
#include <QDateTime>

struct Snippet {
    int id;
    QString title;
    QString content;
    QDateTime created;
    QDateTime modified;
};

class Database
{
public:
    static Database& instance();
    bool initialize();
    
    QList<Snippet> getAllSnippets();
    QList<Snippet> searchSnippets(const QString& searchTerm);
    bool addSnippet(const QString& title, const QString& content);
    bool updateSnippet(int id, const QString& title, const QString& content);
    bool deleteSnippet(int id);
    Snippet getSnippet(int id);

private:
    Database() = default;
    QSqlDatabase m_db;
    bool createTables();
};

#endif // DATABASE_H