#include "database.h"
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

// Creates a SQLite file at path by running the given statements on a separate connection
static void makeDatabase(const QString& path, const QStringList& statements)
{
    {
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", "setup");
        db.setDatabaseName(path);
        QVERIFY(db.open());
        for (const QString& statement : statements) {
            QSqlQuery query(db);
            QVERIFY2(query.exec(statement), qPrintable(statement));
        }
        db.close();
    }
    QSqlDatabase::removeDatabase("setup");
}

static QByteArray readFile(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

// Unix permissions of a file, e.g. "600". Windows has no equivalent, so
// there it always gives "600" and the checks pass trivially.
static QString permissions(const QString& path)
{
#ifdef Q_OS_UNIX
    return QString::number(int(QFile::permissions(path)) & 0x0FFF, 16);
#else
    Q_UNUSED(path)
    return "600";
#endif
}

static const char *V1Schema =
    "CREATE TABLE snippets (id INTEGER PRIMARY KEY AUTOINCREMENT, title TEXT NOT NULL, content TEXT NOT NULL, "
    "created DATETIME DEFAULT CURRENT_TIMESTAMP, modified DATETIME DEFAULT CURRENT_TIMESTAMP)";

class TestDatabase : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        QVERIFY(m_dir.isValid());
        QSettings().clear();
        QFile::remove(Database::defaultDatabasePath());
    }

    void defaultDatabaseIsPrivate()
    {
        QString path = Database::defaultDatabasePath();
        QDir().mkpath(QFileInfo(path).path());
        makeDatabase(path, {V1Schema});
        QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup | QFile::ReadOther);

        QString error, warning;
        QVERIFY2(Database::instance().initialize(&error, &warning), qPrintable(error));
        QVERIFY(warning.isEmpty());
        QCOMPARE(permissions(path), QString("600"));
    }

    void searchTreatsWildcardsLiterally()
    {
        Database& db = Database::instance();
        QVERIFY(db.createDatabase(m_dir.filePath("search.db")));
        db.addSnippet("snake_case", "a");
        db.addSnippet("snakeXcase", "b");
        db.addSnippet("100%", "c");
        db.addSnippet("back\\slash", "d");
        QCOMPARE(search("e_c").size(), 1);
        QCOMPARE(search("%").size(), 1);
        QCOMPARE(search("\\").size(), 1);
        QCOMPARE(search("snake").size(), 2);
    }

    void updateReportsMissingSnippet()
    {
        Database& db = Database::instance();
        QVERIFY(db.createDatabase(m_dir.filePath("update.db")));
        int id = db.addSnippet("gone", "x");
        QVERIFY(db.deleteSnippet(id));
        Snippet snippet;
        snippet.id = id;
        snippet.title = "t";
        QString error;
        QVERIFY(!db.updateSnippet(snippet, &error));
        QVERIFY(!error.isEmpty());
    }

    void createAndMoveArePrivate()
    {
        Database& db = Database::instance();
        QString created = m_dir.filePath("new.db");
        QVERIFY(db.createDatabase(created));
        QCOMPARE(permissions(created), QString("600"));

        QFile::setPermissions(created, QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup | QFile::ReadOther);
        QString moved = m_dir.filePath("sub/moved.db");
        QString error;
        QVERIFY2(db.moveDatabase(moved, &error), qPrintable(error));
        QCOMPARE(permissions(moved), QString("600"));
        QVERIFY(!QFile::exists(created));
    }

    void foreignDatabaseIsLeftUnchanged()
    {
        Database& db = Database::instance();
        QVERIFY(db.createDatabase(m_dir.filePath("current.db")));
        QString current = db.databasePath();

        QString foreign = m_dir.filePath("foreign.sqlite");
        makeDatabase(foreign, {"CREATE TABLE places (id INTEGER)"});
        QByteArray before = readFile(foreign);
        QString error;
        QVERIFY(!db.openDatabase(foreign, &error));
        QVERIFY(error.contains("not a snippets database"));
        QCOMPARE(readFile(foreign), before);
        QCOMPARE(db.databasePath(), current);
    }

    void wrongSchemaIsRejected()
    {
        QString wrong = m_dir.filePath("wrong.db");
        makeDatabase(wrong, {"CREATE TABLE snippets (id INTEGER, body TEXT)"});
        QString error;
        QVERIFY(!Database::instance().openDatabase(wrong, &error));
        QVERIFY(error.contains("doesn't recognise"));
    }

    void nonSqliteFileIsLeftUnchanged()
    {
        QString text = m_dir.filePath("notes.db");
        QFile file(text);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("just some text\n");
        file.close();
        QString error;
        QVERIFY(!Database::instance().openDatabase(text, &error));
        QCOMPARE(readFile(text), QByteArray("just some text\n"));
    }

    void emptyFileBecomesDatabase()
    {
        QString empty = m_dir.filePath("empty.db");
        QFile file(empty);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
        QString error;
        QVERIFY2(Database::instance().openDatabase(empty, &error), qPrintable(error));
    }

    void startupFallsBackToDefault()
    {
        QString text = m_dir.filePath("broken.db");
        QFile file(text);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("not sqlite");
        file.close();
        QSettings().setValue("database/path", text);

        QString error, warning;
        QVERIFY(Database::instance().initialize(&error, &warning));
        QVERIFY(!warning.isEmpty());
        QCOMPARE(Database::instance().databasePath(), QFileInfo(Database::defaultDatabasePath()).absoluteFilePath());
    }

    void upgradesVersion1Database()
    {
        QString path = m_dir.filePath("old.db");
        makeDatabase(path, {V1Schema,
                            "INSERT INTO snippets (title, content, created, modified) "
                            "VALUES ('old one', 'kept', '2025-01-02 03:04:05', '2025-01-02 03:04:05')"});
        QByteArray original = readFile(path);

        Database& db = Database::instance();
        QString error;
        QVERIFY2(db.openDatabase(path, &error), qPrintable(error));
        QString notice = db.takeUpgradeNotice();
        QVERIFY(notice.contains("old-v0-backup.db"));
        QVERIFY(db.takeUpgradeNotice().isEmpty());

        // The backup has the original data and is private
        QString backup = m_dir.filePath("old-v0-backup.db");
        QVERIFY(QFile::exists(backup));
        QCOMPARE(permissions(backup), QString("600"));
        QVERIFY(!original.isEmpty());

        QList<Snippet> all = db.snippets();
        QCOMPARE(all.size(), 1);
        QCOMPARE(all[0].title, QString("old one"));
        QCOMPARE(all[0].content, QString("kept"));
        QCOMPARE(all[0].format, Snippet::PlainText);
        QVERIFY(all[0].tags.isEmpty());
        QCOMPARE(all[0].created.toUTC(), QDateTime(QDate(2025, 1, 2), QTime(3, 4, 5), QTimeZone::utc()));

        // Opening it again doesn't upgrade or back up again
        QVERIFY(db.openDatabase(path, &error));
        QVERIFY(db.takeUpgradeNotice().isEmpty());
        QVERIFY(!QFile::exists(m_dir.filePath("old-v0-backup-2.db")));
    }

    void refusesNewerDatabase()
    {
        QString path = m_dir.filePath("future.db");
        makeDatabase(path, {V1Schema, "PRAGMA user_version = 99"});
        QByteArray before = readFile(path);
        QString error;
        QVERIFY(!Database::instance().openDatabase(path, &error));
        QVERIFY(error.contains("newer version"));
        QCOMPARE(readFile(path), before);
    }

    void savesFormatLanguageAndTags()
    {
        Database& db = Database::instance();
        QVERIFY(db.createDatabase(m_dir.filePath("fields.db")));

        Snippet snippet;
        snippet.title = "Script";
        snippet.content = "echo hi";
        snippet.language = "Bash";
        snippet.tags = {" shell ", "Work", "work", "", "a,b"};
        QString error;
        int id = db.addSnippet(snippet, &error);
        QVERIFY2(id != -1, qPrintable(error));

        Snippet saved = db.snippet(id);
        QCOMPARE(saved.language, QString("Bash"));
        QCOMPARE(saved.format, Snippet::PlainText);
        QCOMPARE(saved.tags, QStringList({"a b", "shell", "Work"}));
        QVERIFY(qAbs(saved.created.secsTo(QDateTime::currentDateTime())) < 60);

        saved.format = Snippet::RichText;
        saved.content = "**bold**";
        saved.tags = {"shell"};
        QVERIFY(db.updateSnippet(saved, &error));
        Snippet updated = db.snippet(id);
        QCOMPARE(updated.format, Snippet::RichText);
        QCOMPARE(updated.content, QString("**bold**"));
        QCOMPARE(updated.tags, QStringList({"shell"}));
    }

    void filtersByTag()
    {
        Database& db = Database::instance();
        QVERIFY(db.createDatabase(m_dir.filePath("tags.db")));
        Snippet a; a.title = "a"; a.tags = {"Work", "shell"};
        Snippet b; b.title = "b"; b.tags = {"work"};
        Snippet c; c.title = "c";
        db.addSnippet(a);
        int bId = db.addSnippet(b);
        db.addSnippet(c);

        SnippetFilter filter;
        filter.tag = "WORK";  // tags match case-insensitively
        QCOMPARE(db.snippets(filter).size(), 2);
        filter = {};
        filter.untagged = true;
        QCOMPARE(db.snippets(filter).size(), 1);
        filter = {};
        filter.search = "shell";  // search includes tags
        QCOMPARE(db.snippets(filter).size(), 1);

        QList<QPair<QString, int>> counts = db.tagCounts();
        QCOMPARE(counts.size(), 2);
        QCOMPARE(counts[0], qMakePair(QString("shell"), 1));
        QCOMPARE(counts[1], qMakePair(QString("Work"), 2));

        QVERIFY(db.deleteSnippet(bId));
        QCOMPARE(db.tagCounts()[1], qMakePair(QString("Work"), 1));
    }

    void tidiesTagsLeftByOlderVersions()
    {
        Database& db = Database::instance();
        QString path = m_dir.filePath("orphans.db");
        QVERIFY(db.createDatabase(path));
        Snippet a; a.title = "a"; a.tags = {"lonely"};
        int id = db.addSnippet(a);
        QVERIFY(db.createDatabase(m_dir.filePath("other.db")));

        // 1.x deletes snippets without knowing about tags
        makeDatabase(path, {QString("DELETE FROM snippets WHERE id = %1").arg(id)});
        QVERIFY(db.openDatabase(path));
        QVERIFY(db.tagCounts().isEmpty());
    }

    void searchRanksAndMatchesParts()
    {
        Database& db = Database::instance();
        QVERIFY(db.createDatabase(m_dir.filePath("fts.db")));
        QVERIFY(db.hasSearchIndex());
        int body = db.addSnippet("Shopping", "remember the deployment keys");
        int title = db.addSnippet("Deployment steps", "run it");
        Snippet tagged;
        tagged.title = "Other";
        tagged.tags = {"deployment"};
        int tag = db.addSnippet(tagged);

        // Title matches come first, then tags, then content
        QList<Snippet> found = search("deploy");
        QCOMPARE(found.size(), 3);
        QCOMPARE(found[0].id, title);
        QCOMPARE(found[1].id, tag);
        QCOMPARE(found[2].id, body);

        // Part of a word, every word must match, and short words still work
        QCOMPARE(search("ploym").size(), 3);
        QCOMPARE(search("deploy keys").size(), 1);
        QCOMPARE(search("it").size(), 1);
        QCOMPARE(search("run deploy").size(), 1);
    }

    void searchIndexFollowsChanges()
    {
        Database& db = Database::instance();
        QVERIFY(db.createDatabase(m_dir.filePath("fts-changes.db")));
        int id = db.addSnippet("first", "alpha");
        QCOMPARE(search("alpha").size(), 1);

        Snippet changed = db.snippet(id);
        changed.content = "omega";
        QVERIFY(db.updateSnippet(changed));
        QCOMPARE(search("alpha").size(), 0);
        QCOMPARE(search("omega").size(), 1);

        QVERIFY(db.deleteSnippet(id));
        QCOMPARE(search("omega").size(), 0);
    }

    void searchFindsRenderedRichText()
    {
        Database& db = Database::instance();
        QVERIFY(db.createDatabase(m_dir.filePath("fts-rich.db")));
        Snippet rich;
        rich.title = "Rich";
        rich.format = Snippet::RichText;
        rich.content = "call **my\\_function** now";
        db.addSnippet(rich);
        QCOMPARE(search("my_function").size(), 1);
        QCOMPARE(search("**").size(), 0);
    }

    void searchHandlesSearchSyntax()
    {
        Database& db = Database::instance();
        QVERIFY(db.createDatabase(m_dir.filePath("fts-syntax.db")));
        db.addSnippet("quote", "say \"hello\" AND -x OR y* NEAR(z)");
        const QStringList terms = {"\"hello\"", "AND", "-x", "y*", "NEAR(z)", "\"", "(((", "OR"};
        for (const QString &term : terms) {
            search(term);  // mustn't fail
        }
        QCOMPARE(search("\"hello\"").size(), 1);
        QCOMPARE(search("NEAR(z)").size(), 1);
    }

    void searchIndexIsRebuiltOnOpen()
    {
        // Changes made by other versions (or other programs) are picked up
        Database& db = Database::instance();
        QString path = m_dir.filePath("fts-reopen.db");
        QVERIFY(db.createDatabase(path));
        db.addSnippet("kept", "x");
        QVERIFY(db.createDatabase(m_dir.filePath("fts-other.db")));
        makeDatabase(path, {"INSERT INTO snippets (title, content) VALUES ('added elsewhere', 'y')"});
        QVERIFY(db.openDatabase(path));
        QCOMPARE(search("elsewhere").size(), 1);
    }

    void trashKeepsSnippetsUntilEmptied()
    {
        Database& db = Database::instance();
        QVERIFY(db.createDatabase(m_dir.filePath("trash.db")));
        Snippet tagged;
        tagged.title = "tagged";
        tagged.tags = {"keep"};
        int id = db.addSnippet(tagged);
        int other = db.addSnippet("other", "");

        QVERIFY(db.moveToTrash(id));
        QVERIFY(!db.moveToTrash(id));  // already there
        QCOMPARE(db.snippets().size(), 1);
        QCOMPARE(db.trashCount(), 1);
        // Tag counts only include snippets that aren't in the trash, but the tag is kept
        QVERIFY(db.tagCounts().isEmpty());
        SnippetFilter trash;
        trash.trash = true;
        QList<Snippet> trashed = db.snippets(trash);
        QCOMPARE(trashed.size(), 1);
        QVERIFY(trashed[0].inTrash());
        QCOMPARE(trashed[0].tags, QStringList({"keep"}));

        QVERIFY(db.restoreFromTrash(id));
        QCOMPARE(db.snippet(id).tags, QStringList({"keep"}));
        QVERIFY(!db.snippet(id).inTrash());

        QVERIFY(db.moveToTrash(id));
        QVERIFY(db.moveToTrash(other));
        QVERIFY(db.emptyTrash());
        QCOMPARE(db.trashCount(), 0);
        QCOMPARE(db.snippets().size(), 0);
        QVERIFY(!db.snippet(id).isValid());
    }

    void foldersHoldSnippetsAndMoveUpWhenDeleted()
    {
        Database& db = Database::instance();
        QVERIFY(db.createDatabase(m_dir.filePath("folders.db")));
        QString error;
        int work = db.addFolder("Work", -1, &error);
        QVERIFY2(work != -1, qPrintable(error));
        int scripts = db.addFolder(" Scripts/Shell ", work, &error);
        QVERIFY(scripts != -1);
        QCOMPARE(db.folderPath(scripts), QString("Work/Scripts-Shell"));

        // Names are unique among siblings, ignoring case
        QVERIFY(db.addFolder("work", -1, &error) == -1);
        QVERIFY(error.contains("already"));
        QVERIFY(db.addFolder("   ", -1, &error) == -1);
        QVERIFY(db.addFolder("Work", scripts) != -1);  // fine elsewhere

        Snippet snippet;
        snippet.title = "deploy";
        snippet.folderId = scripts;
        int id = db.addSnippet(snippet);
        QCOMPARE(db.snippet(id).folderId, scripts);
        QCOMPARE(db.snippet(id).folderPath, QString("Work/Scripts-Shell"));
        QCOMPARE(db.snippets().value(0).folderPath, QString("Work/Scripts-Shell"));

        // Saving the snippet's other fields doesn't move it
        Snippet saved = db.snippet(id);
        saved.folderId = -1;
        saved.title = "deploy 2";
        QVERIFY(db.updateSnippet(saved));
        QCOMPARE(db.snippet(id).folderId, scripts);

        QVERIFY(db.renameFolder(scripts, "Shell", &error));
        QVERIFY(!db.renameFolder(scripts, "", &error));

        // A folder can't go inside itself or its own subfolder
        QVERIFY(!db.moveFolder(work, scripts, &error));
        QVERIFY(error.contains("itself"));
        QVERIFY(!db.moveFolder(work, work, &error));

        // Deleting Work moves Shell (and its snippet) up to the top level
        QVERIFY(db.deleteFolder(work, &error));
        QCOMPARE(db.folderPath(scripts), QString("Shell"));
        QCOMPARE(db.snippet(id).folderId, scripts);
        QVERIFY(db.deleteFolder(scripts, &error));
        QCOMPARE(db.snippet(id).folderId, -1);

        QVERIFY(db.moveSnippetToFolder(id, db.folderForPath("A/B", true)));
        QCOMPARE(db.snippet(id).folderPath, QString("A/B"));
        QCOMPARE(db.folderForPath("a/b", false), db.snippet(id).folderId);
        QCOMPARE(db.folderForPath("A/missing", false), -1);
    }

    void upgradesVersion2Database()
    {
        QString path = m_dir.filePath("v2.db");
        makeDatabase(path, {QString(V1Schema).replace("modified DATETIME DEFAULT CURRENT_TIMESTAMP)",
                                                      "modified DATETIME DEFAULT CURRENT_TIMESTAMP, "
                                                      "format TEXT NOT NULL DEFAULT 'plain', language TEXT NOT NULL DEFAULT '', "
                                                      "deleted_at DATETIME)"),
                            "CREATE TABLE tags (id INTEGER PRIMARY KEY, name TEXT NOT NULL UNIQUE COLLATE NOCASE)",
                            "CREATE TABLE snippet_tags (snippet_id INTEGER NOT NULL, tag_id INTEGER NOT NULL, PRIMARY KEY (snippet_id, tag_id))",
                            "INSERT INTO snippets (title, content) VALUES ('kept', 'x')",
                            "PRAGMA user_version = 2"});
        Database& db = Database::instance();
        QString error;
        QVERIFY2(db.openDatabase(path, &error), qPrintable(error));
        QVERIFY(QFile::exists(m_dir.filePath("v2-v2-backup.db")));
        QVERIFY(db.takeUpgradeNotice().contains("folders"));
        QCOMPARE(db.snippets().size(), 1);
        QCOMPARE(db.snippets().value(0).folderId, -1);
        QVERIFY(db.addFolder("New", -1) != -1);
    }

private:
    static QList<Snippet> search(const QString& term)
    {
        SnippetFilter filter;
        filter.search = term;
        return Database::instance().snippets(filter);
    }

    QTemporaryDir m_dir;
};

QTEST_MAIN(TestDatabase)
#include "tst_database.moc"
