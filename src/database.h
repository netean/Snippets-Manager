#ifndef DATABASE_H
#define DATABASE_H

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QString>
#include <QDateTime>

struct Snippet {
    int id = -1;
    QString title;
    QString content;
    QDateTime created;
    QDateTime modified;
};

class Database
{
public:
    enum SortOrder {
        TitleAscending = 0,
        TitleDescending,
        CreatedAscending,
        CreatedDescending
    };

    static Database& instance();
    bool initialize();

    // Database file management
    static QString defaultDatabasePath();
    QString databasePath() const;
    bool openDatabase(const QString& path, QString* errorMessage = nullptr);
    bool createDatabase(const QString& path, QString* errorMessage = nullptr);
    bool moveDatabase(const QString& newPath, QString* errorMessage = nullptr);

    QList<Snippet> getAllSnippets(SortOrder order = CreatedDescending);
    QList<Snippet> searchSnippets(const QString& searchTerm, SortOrder order = CreatedDescending);
    int addSnippet(const QString& title, const QString& content);
    bool updateSnippet(int id, const QString& title, const QString& content);
    bool deleteSnippet(int id);
    Snippet getSnippet(int id);

private:
    Database() = default;
    QSqlDatabase m_db;
    bool createTables();
    bool openAt(const QString& path, QString* errorMessage);
    static QString orderByClause(SortOrder order);
    static QList<Snippet> readSnippets(QSqlQuery& query);
};

#endif // DATABASE_H
