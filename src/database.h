#ifndef DATABASE_H
#define DATABASE_H

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QString>
#include <QStringList>
#include <QDateTime>
#include <QList>
#include <QPair>

struct Snippet {
    enum Format { PlainText, RichText };

    int id = -1;
    QString title;
    // Plain text, or Markdown when format is RichText
    QString content;
    Format format = PlainText;
    // Syntax highlighting language (a KSyntaxHighlighting definition name), or empty
    QString language;
    QStringList tags;
    // In local time. deleted is only valid while the snippet is in the trash.
    QDateTime created;
    QDateTime modified;
    QDateTime deleted;

    bool isValid() const { return id != -1; }
    bool isRich() const { return format == RichText; }
    bool inTrash() const { return deleted.isValid(); }
};

// Which snippets to list
struct SnippetFilter {
    QString search;
    QString tag;            // only snippets with this tag (empty for any)
    bool untagged = false;  // only snippets without tags
    bool trash = false;     // snippets in the trash instead of the normal ones
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

    // Version of the database layout. 1.x databases have version 0 and are
    // upgraded (after making a backup) when they're opened.
    static constexpr int SchemaVersion = 2;

    static Database& instance();

    // Opens the saved database, falling back to the default location if it
    // can't be opened. On fallback, warningMessage explains what happened.
    bool initialize(QString* errorMessage = nullptr, QString* warningMessage = nullptr);

    // Database file management
    static QString defaultDatabasePath();
    QString databasePath() const;
    bool openDatabase(const QString& path, QString* errorMessage = nullptr);
    bool createDatabase(const QString& path, QString* errorMessage = nullptr);
    bool moveDatabase(const QString& newPath, QString* errorMessage = nullptr);

    // Set when opening the database upgraded it from an older version;
    // describes the upgrade and where the backup is. Cleared when read.
    QString takeUpgradeNotice();

    // Snippets
    QList<Snippet> snippets(const SnippetFilter& filter = {}, SortOrder order = CreatedDescending);
    Snippet snippet(int id);
    int addSnippet(const Snippet& snippet, QString* errorMessage = nullptr);
    int addSnippet(const QString& title, const QString& content);
    // Saves the title, content, format, language and tags
    bool updateSnippet(const Snippet& snippet, QString* errorMessage = nullptr);
    // Deletes permanently (see moveToTrash)
    bool deleteSnippet(int id, QString* errorMessage = nullptr);

    // Whether searches use the full-text index (ranked by relevance), rather
    // than a plain substring search
    bool hasSearchIndex() const { return m_searchIndex; }

    // Tags in use by snippets that aren't in the trash, with how many use each
    QList<QPair<QString, int>> tagCounts();
    // Cleans up a list of tags: trims them and drops empty and repeated ones
    static QStringList normalizeTags(const QStringList& tags);

private:
    Database() = default;
    QSqlDatabase m_db;
    QString m_upgradeNotice;
    bool m_searchIndex = false;

    bool openAt(const QString& path, QString* errorMessage);
    bool prepareSchema(QString* errorMessage);
    bool createTables();
    bool upgradeSchema(int fromVersion, QString* errorMessage);
    void tidyUp();
    void buildSearchIndex();
    void indexSnippet(int id);
    void unindexSnippet(int id);
    bool writeTags(int id, const QStringList& tags);
    QHash<int, QStringList> readAllTags();
    bool exec(QSqlQuery& query, QString* errorMessage);
    static void restrictPermissions(const QString& path);
    static QString orderByClause(SortOrder order);
    static Snippet readSnippet(const QSqlQuery& query);
    static QDateTime fromSqlTime(const QVariant& value);
};

#endif // DATABASE_H
