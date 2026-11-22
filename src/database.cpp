#include "database.h"
#include <QSqlError>
#include <QStandardPaths>
#include <QSettings>
#include <QFileInfo>
#include <QFile>
#include <QDir>
#include <QDebug>
#include <QStringList>
#include <QHash>
#include <QSet>
#include <QTimeZone>

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

bool Database::initialize(QString* errorMessage, QString* warningMessage)
{
    QSettings settings;
    QString path = settings.value(DatabasePathKey, defaultDatabasePath()).toString();
    QString defaultPath = defaultDatabasePath();
    bool isDefault = QFileInfo(path).absoluteFilePath() == QFileInfo(defaultPath).absoluteFilePath();

    QString error;
    if (openAt(path, &error)) {
        // Only this app uses the default database, so it's always safe to make it
        // private. This also tightens databases created by earlier versions.
        if (isDefault) {
            restrictPermissions(databasePath());
        }
        return true;
    }

    qWarning() << "Failed to open database" << path << ":" << error;

    // Fall back to the default location if a custom database can't be opened
    // (e.g. it lives on a drive that isn't mounted). The saved setting is kept
    // so the custom database is tried again on the next start.
    if (!isDefault) {
        QString defaultError;
        if (openAt(defaultPath, &defaultError)) {
            restrictPermissions(databasePath());
            if (warningMessage) {
                *warningMessage = QString("Your database '%1' could not be opened, so the default database '%2' "
                                          "is being used instead. Snippets you add or change now are saved there.\n\n%3")
                                      .arg(QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath()),
                                           QDir::toNativeSeparators(databasePath()), error);
            }
            return true;
        }
        qWarning() << "Failed to open default database:" << defaultError;
        error += "\n\nThe default database could not be opened either: " + defaultError;
    }

    if (errorMessage) *errorMessage = error;
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
    bool isNewFile = !QFileInfo::exists(absolutePath);
    QDir().mkpath(QFileInfo(absolutePath).absolutePath());
    m_db.setDatabaseName(absolutePath);

    if (!m_db.open()) {
        if (errorMessage) *errorMessage = m_db.lastError().text();
        return false;
    }

    if (!prepareSchema(errorMessage)) {
        m_db.close();
        return false;
    }

    if (isNewFile) {
        restrictPermissions(absolutePath);
    }
    return true;
}

bool Database::prepareSchema(QString* errorMessage)
{
    QString path = QDir::toNativeSeparators(m_db.databaseName());
    QSqlQuery query(m_db);

    // Stops views and triggers stored in a database file that came from
    // elsewhere from calling SQL functions that have side effects.
    query.exec("PRAGMA trusted_schema = OFF");
    // Removing a snippet also removes its tags (see snippet_tags)
    query.exec("PRAGMA foreign_keys = ON");

    // SQLite opens lazily, so this first read is also what tells us whether
    // the file is actually a SQLite database.
    if (!query.exec(R"(SELECT name FROM sqlite_master WHERE type = 'table' AND name NOT LIKE 'sqlite\_%' ESCAPE '\')")) {
        if (errorMessage) *errorMessage = QString("'%1' is not a SQLite database (%2).").arg(path, query.lastError().text());
        return false;
    }
    QStringList tables;
    while (query.next()) {
        tables.append(query.value(0).toString());
    }

    if (!tables.contains("snippets")) {
        // Don't add our tables to some other program's database
        if (!tables.isEmpty()) {
            if (errorMessage) *errorMessage = QString("'%1' is a SQLite database, but not a snippets database. "
                                                      "The file was left unchanged.").arg(path);
            return false;
        }
        if (!createTables() || !upgradeSchema(0, errorMessage)) {
            if (errorMessage && errorMessage->isEmpty()) {
                *errorMessage = QString("The snippets table could not be created in '%1'.").arg(path);
            }
            return false;
        }
        return true;
    }

    // Check the existing table has the columns every version reads and writes
    QStringList columns;
    if (query.exec("PRAGMA table_info(snippets)")) {
        while (query.next()) {
            columns.append(query.value(1).toString());
        }
    }
    for (const char *required : {"id", "title", "content", "created", "modified"}) {
        if (!columns.contains(QString::fromLatin1(required))) {
            if (errorMessage) *errorMessage = QString("'%1' has a snippets table that this app doesn't recognise "
                                                      "(it has no '%2' column). The file was left unchanged.")
                                                  .arg(path, QString::fromLatin1(required));
            return false;
        }
    }

    int version = query.exec("PRAGMA user_version") && query.next() ? query.value(0).toInt() : 0;
    // VACUUM and schema changes can't run while a statement is still active
    query.finish();
    if (version > SchemaVersion) {
        if (errorMessage) *errorMessage = QString("'%1' was made by a newer version of Snippet Manager. "
                                                  "Update Snippet Manager to open it. The file was left unchanged.").arg(path);
        return false;
    }
    if (version < SchemaVersion) {
        // Keep a copy of the database as it was, in case the upgrade goes
        // wrong or the user wants to go back to the older version
        QFileInfo info(m_db.databaseName());
        QString backup = info.absolutePath() + "/" + info.completeBaseName() + QString("-v%1-backup.db").arg(version);
        for (int n = 2; QFileInfo::exists(backup); ++n) {
            backup = info.absolutePath() + "/" + info.completeBaseName() + QString("-v%1-backup-%2.db").arg(version).arg(n);
        }
        QSqlQuery vacuum(m_db);
        vacuum.prepare("VACUUM INTO ?");
        vacuum.addBindValue(backup);
        if (!vacuum.exec()) {
            if (errorMessage) *errorMessage = QString("'%1' needs upgrading, but a backup copy could not be made first, "
                                                      "so it was left unchanged (%2).").arg(path, vacuum.lastError().text());
            return false;
        }
        restrictPermissions(backup);

        if (!upgradeSchema(version, errorMessage)) {
            return false;
        }
        m_upgradeNotice = QString("Your snippets database was upgraded for Snippet Manager %1. "
                                  "A copy of it as it was before the upgrade has been saved to:\n\n%2\n\n"
                                  "Older versions of Snippet Manager can still open the upgraded database, but they "
                                  "won't show tags or formatting, and they show snippets that are in the trash.")
                              .arg(QString(APP_VERSION), QDir::toNativeSeparators(backup));
    }

    tidyUp();
    return true;
}

void Database::restrictPermissions(const QString& path)
{
    // Snippets often hold tokens and passwords, so keep the file private to the
    // current user, wherever it's stored. (On Windows this only affects the
    // read-only flag, so it's harmless there.)
    if (!QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                     | QFileDevice::ReadUser | QFileDevice::WriteUser)) {
        qWarning() << "Could not restrict the permissions of" << path;
    }
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
    // QFile::copy keeps the old file's permissions, which may be readable by everyone
    restrictPermissions(absoluteNewPath);

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
    // The layout used by 1.x; upgradeSchema() brings it up to date
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
        qWarning() << "Failed to create table:" << query.lastError().text();
        return false;
    }
    return true;
}

bool Database::upgradeSchema(int fromVersion, QString* errorMessage)
{
    if (!m_db.transaction()) {
        if (errorMessage) *errorMessage = m_db.lastError().text();
        return false;
    }

    QSqlQuery query(m_db);
    QStringList statements;

    if (fromVersion < 2) {
        // Only add the columns that are missing, in case an earlier upgrade was interrupted
        QStringList columns;
        if (query.exec("PRAGMA table_info(snippets)")) {
            while (query.next()) {
                columns.append(query.value(1).toString());
            }
        }
        if (!columns.contains("format")) {
            statements << "ALTER TABLE snippets ADD COLUMN format TEXT NOT NULL DEFAULT 'plain'";
        }
        if (!columns.contains("language")) {
            statements << "ALTER TABLE snippets ADD COLUMN language TEXT NOT NULL DEFAULT ''";
        }
        if (!columns.contains("deleted_at")) {
            statements << "ALTER TABLE snippets ADD COLUMN deleted_at DATETIME";
        }
        statements << "CREATE TABLE IF NOT EXISTS tags ("
                      "id INTEGER PRIMARY KEY, name TEXT NOT NULL UNIQUE COLLATE NOCASE)"
                   << "CREATE TABLE IF NOT EXISTS snippet_tags ("
                      "snippet_id INTEGER NOT NULL REFERENCES snippets(id) ON DELETE CASCADE, "
                      "tag_id INTEGER NOT NULL REFERENCES tags(id) ON DELETE CASCADE, "
                      "PRIMARY KEY (snippet_id, tag_id))";
    }
    statements << QString("PRAGMA user_version = %1").arg(SchemaVersion);

    for (const QString& statement : statements) {
        if (!query.exec(statement)) {
            if (errorMessage) *errorMessage = QString("The database could not be upgraded (%1).").arg(query.lastError().text());
            m_db.rollback();
            return false;
        }
    }
    return m_db.commit();
}

void Database::tidyUp()
{
    // Older versions don't know about tags, so deleting a snippet there
    // leaves its tags behind
    QSqlQuery query(m_db);
    query.exec("DELETE FROM snippet_tags WHERE snippet_id NOT IN (SELECT id FROM snippets)");
    query.exec("DELETE FROM tags WHERE id NOT IN (SELECT tag_id FROM snippet_tags)");
}

QString Database::takeUpgradeNotice()
{
    QString notice = m_upgradeNotice;
    m_upgradeNotice.clear();
    return notice;
}

bool Database::exec(QSqlQuery& query, QString* errorMessage)
{
    if (query.exec()) {
        return true;
    }
    if (errorMessage) *errorMessage = query.lastError().text();
    qWarning() << "Query failed:" << query.lastQuery() << query.lastError().text();
    return false;
}

// ---------------------------------------------------------------------------
// Reading snippets
// ---------------------------------------------------------------------------

static const char *SnippetColumns = "s.id, s.title, s.content, s.format, s.language, s.created, s.modified, s.deleted_at";

static QString formatName(Snippet::Format format)
{
    return format == Snippet::RichText ? "rich" : "plain";
}

// Qt binds a null QString as SQL NULL, which the NOT NULL text columns reject
static QString text(const QString& value)
{
    return value.isNull() ? QString("") : value;
}

static QString toSqlTime(const QDateTime& time)
{
    return time.toUTC().toString("yyyy-MM-dd HH:mm:ss");
}

QDateTime Database::fromSqlTime(const QVariant& value)
{
    // SQLite's CURRENT_TIMESTAMP is UTC, written as "YYYY-MM-DD HH:MM:SS"
    if (value.isNull()) {
        return {};
    }
    QDateTime time = QDateTime::fromString(value.toString().replace(' ', 'T'), Qt::ISODate);
    if (!time.isValid()) {
        return {};
    }
    time.setTimeZone(QTimeZone::utc());
    return time.toLocalTime();
}

Snippet Database::readSnippet(const QSqlQuery& query)
{
    Snippet snippet;
    snippet.id = query.value(0).toInt();
    snippet.title = query.value(1).toString();
    snippet.content = query.value(2).toString();
    snippet.format = query.value(3).toString() == "rich" ? Snippet::RichText : Snippet::PlainText;
    snippet.language = query.value(4).toString();
    snippet.created = fromSqlTime(query.value(5));
    snippet.modified = fromSqlTime(query.value(6));
    snippet.deleted = fromSqlTime(query.value(7));
    return snippet;
}

QHash<int, QStringList> Database::readAllTags()
{
    QHash<int, QStringList> tags;
    QSqlQuery query(m_db);
    if (query.exec("SELECT st.snippet_id, t.name FROM snippet_tags st JOIN tags t ON t.id = st.tag_id "
                   "ORDER BY t.name COLLATE NOCASE")) {
        while (query.next()) {
            tags[query.value(0).toInt()].append(query.value(1).toString());
        }
    }
    return tags;
}

QString Database::orderByClause(SortOrder order)
{
    // id breaks ties so snippets created in the same second keep a stable order
    switch (order) {
    case TitleAscending:
        return "ORDER BY s.title COLLATE NOCASE ASC, s.id ASC";
    case TitleDescending:
        return "ORDER BY s.title COLLATE NOCASE DESC, s.id DESC";
    case CreatedAscending:
        return "ORDER BY s.created ASC, s.id ASC";
    case CreatedDescending:
    default:
        return "ORDER BY s.created DESC, s.id DESC";
    }
}

// Escapes a search term for LIKE ... ESCAPE '\', so % and _ are matched literally
static QString likePattern(const QString& term)
{
    QString escaped = term;
    escaped.replace('\\', "\\\\").replace('%', "\\%").replace('_', "\\_");
    return "%" + escaped + "%";
}

QList<Snippet> Database::snippets(const SnippetFilter& filter, SortOrder order)
{
    QStringList conditions;
    QVariantList values;

    conditions << (filter.trash ? "s.deleted_at IS NOT NULL" : "s.deleted_at IS NULL");

    const QStringList terms = filter.search.split(' ', Qt::SkipEmptyParts);
    for (const QString& term : terms) {
        conditions << R"((s.title LIKE ? ESCAPE '\' OR s.content LIKE ? ESCAPE '\' OR EXISTS (
                            SELECT 1 FROM snippet_tags st JOIN tags t ON t.id = st.tag_id
                            WHERE st.snippet_id = s.id AND t.name LIKE ? ESCAPE '\')))";
        values << likePattern(term) << likePattern(term) << likePattern(term);
    }

    if (filter.untagged) {
        conditions << "NOT EXISTS (SELECT 1 FROM snippet_tags st WHERE st.snippet_id = s.id)";
    } else if (!filter.tag.isEmpty()) {
        conditions << "EXISTS (SELECT 1 FROM snippet_tags st JOIN tags t ON t.id = st.tag_id "
                      "WHERE st.snippet_id = s.id AND t.name = ?)";
        values << filter.tag;
    }

    QSqlQuery query(m_db);
    query.prepare(QString("SELECT %1 FROM snippets s WHERE %2 %3")
                      .arg(SnippetColumns, conditions.join(" AND "), orderByClause(order)));
    for (const QVariant& value : values) {
        query.addBindValue(value);
    }
    if (!exec(query, nullptr)) {
        return {};
    }

    QHash<int, QStringList> tags = readAllTags();
    QList<Snippet> result;
    while (query.next()) {
        Snippet snippet = readSnippet(query);
        snippet.tags = tags.value(snippet.id);
        result.append(snippet);
    }
    return result;
}

Snippet Database::snippet(int id)
{
    QSqlQuery query(m_db);
    query.prepare(QString("SELECT %1 FROM snippets s WHERE s.id = ?").arg(SnippetColumns));
    query.addBindValue(id);
    if (!exec(query, nullptr) || !query.next()) {
        return {};
    }
    Snippet snippet = readSnippet(query);
    snippet.tags = readAllTags().value(id);
    return snippet;
}

QList<QPair<QString, int>> Database::tagCounts()
{
    QList<QPair<QString, int>> counts;
    QSqlQuery query(m_db);
    if (query.exec("SELECT t.name, COUNT(*) FROM tags t "
                   "JOIN snippet_tags st ON st.tag_id = t.id "
                   "JOIN snippets s ON s.id = st.snippet_id "
                   "WHERE s.deleted_at IS NULL "
                   "GROUP BY t.id ORDER BY t.name COLLATE NOCASE")) {
        while (query.next()) {
            counts.append({query.value(0).toString(), query.value(1).toInt()});
        }
    }
    return counts;
}

// ---------------------------------------------------------------------------
// Changing snippets
// ---------------------------------------------------------------------------

QStringList Database::normalizeTags(const QStringList& tags)
{
    QStringList result;
    QSet<QString> seen;
    for (const QString& tag : tags) {
        // Commas separate tags when they're typed, so they can't be part of one
        QString cleaned = QString(tag).replace(',', ' ').simplified();
        if (!cleaned.isEmpty() && !seen.contains(cleaned.toLower())) {
            seen.insert(cleaned.toLower());
            result.append(cleaned);
        }
    }
    return result;
}

bool Database::writeTags(int id, const QStringList& tags)
{
    QSqlQuery query(m_db);
    query.prepare("DELETE FROM snippet_tags WHERE snippet_id = ?");
    query.addBindValue(id);
    if (!exec(query, nullptr)) return false;

    for (const QString& tag : normalizeTags(tags)) {
        query.prepare("INSERT OR IGNORE INTO tags (name) VALUES (?)");
        query.addBindValue(tag);
        if (!exec(query, nullptr)) return false;

        query.prepare("INSERT OR IGNORE INTO snippet_tags (snippet_id, tag_id) SELECT ?, id FROM tags WHERE name = ?");
        query.addBindValue(id);
        query.addBindValue(tag);
        if (!exec(query, nullptr)) return false;
    }

    // Tags only exist while a snippet uses them
    return query.exec("DELETE FROM tags WHERE id NOT IN (SELECT tag_id FROM snippet_tags)");
}

int Database::addSnippet(const Snippet& snippet, QString* errorMessage)
{
    if (!m_db.transaction()) {
        if (errorMessage) *errorMessage = m_db.lastError().text();
        return -1;
    }

    QSqlQuery query(m_db);
    query.prepare("INSERT INTO snippets (title, content, format, language, created, modified) "
                  "VALUES (?, ?, ?, ?, COALESCE(?, CURRENT_TIMESTAMP), COALESCE(?, CURRENT_TIMESTAMP))");
    query.addBindValue(text(snippet.title));
    query.addBindValue(text(snippet.content));
    query.addBindValue(formatName(snippet.format));
    query.addBindValue(text(snippet.language));
    // Imported snippets keep their dates; new ones get the current time
    query.addBindValue(snippet.created.isValid() ? QVariant(toSqlTime(snippet.created)) : QVariant());
    query.addBindValue(snippet.modified.isValid() ? QVariant(toSqlTime(snippet.modified)) : QVariant());
    if (!exec(query, errorMessage)) {
        m_db.rollback();
        return -1;
    }

    int id = query.lastInsertId().toInt();
    if (!writeTags(id, snippet.tags)) {
        if (errorMessage) *errorMessage = "The snippet's tags could not be saved.";
        m_db.rollback();
        return -1;
    }
    if (!m_db.commit()) {
        if (errorMessage) *errorMessage = m_db.lastError().text();
        return -1;
    }
    return id;
}

int Database::addSnippet(const QString& title, const QString& content)
{
    Snippet snippet;
    snippet.title = title;
    snippet.content = content;
    return addSnippet(snippet);
}

bool Database::updateSnippet(const Snippet& snippet, QString* errorMessage)
{
    if (!m_db.transaction()) {
        if (errorMessage) *errorMessage = m_db.lastError().text();
        return false;
    }

    QSqlQuery query(m_db);
    query.prepare("UPDATE snippets SET title = ?, content = ?, format = ?, language = ?, "
                  "modified = CURRENT_TIMESTAMP WHERE id = ?");
    query.addBindValue(text(snippet.title));
    query.addBindValue(text(snippet.content));
    query.addBindValue(formatName(snippet.format));
    query.addBindValue(text(snippet.language));
    query.addBindValue(snippet.id);
    if (!exec(query, errorMessage)) {
        m_db.rollback();
        return false;
    }
    if (query.numRowsAffected() == 0) {
        if (errorMessage) *errorMessage = "The snippet no longer exists in the database.";
        m_db.rollback();
        return false;
    }
    if (!writeTags(snippet.id, snippet.tags)) {
        if (errorMessage) *errorMessage = "The snippet's tags could not be saved.";
        m_db.rollback();
        return false;
    }
    if (!m_db.commit()) {
        if (errorMessage) *errorMessage = m_db.lastError().text();
        return false;
    }
    return true;
}

bool Database::deleteSnippet(int id, QString* errorMessage)
{
    QSqlQuery query(m_db);
    query.prepare("DELETE FROM snippets WHERE id = ?");
    query.addBindValue(id);
    if (!exec(query, errorMessage)) {
        return false;
    }
    // Its snippet_tags rows go with it; remove tags nothing uses any more
    query.exec("DELETE FROM tags WHERE id NOT IN (SELECT tag_id FROM snippet_tags)");
    return true;
}
