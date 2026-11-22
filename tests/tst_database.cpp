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
        QCOMPARE(db.searchSnippets("e_c").size(), 1);
        QCOMPARE(db.searchSnippets("%").size(), 1);
        QCOMPARE(db.searchSnippets("\\").size(), 1);
        QCOMPARE(db.searchSnippets("snake").size(), 2);
    }

    void updateReportsMissingSnippet()
    {
        Database& db = Database::instance();
        QVERIFY(db.createDatabase(m_dir.filePath("update.db")));
        int id = db.addSnippet("gone", "x");
        QVERIFY(db.deleteSnippet(id));
        QString error;
        QVERIFY(!db.updateSnippet(id, "t", "c", &error));
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

private:
    QTemporaryDir m_dir;
};

QTEST_MAIN(TestDatabase)
#include "tst_database.moc"
