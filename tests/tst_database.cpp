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

static QString permissions(const QString& path)
{
    return QString::number(int(QFile::permissions(path)) & 0x0FFF, 16);
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
