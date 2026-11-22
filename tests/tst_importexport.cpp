#include "database.h"
#include "importexport.h"
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

using namespace ImportExport;

static QList<Snippet> sampleSnippets()
{
    QDateTime created(QDate(2025, 3, 4), QTime(5, 6, 7), QTimeZone::utc());
    QDateTime modified(QDate(2026, 1, 2), QTime(3, 4, 5), QTimeZone::utc());

    Snippet plain;
    plain.title = "Plain, with \"quotes\"";
    plain.content = "Line one, with a comma\nLine \"two\"\n## not a heading\n```\nnot a fence end";
    plain.tags = {"misc", "work"};
    plain.created = created.toLocalTime();
    plain.modified = modified.toLocalTime();

    Snippet rich;
    rich.title = "Rich";
    rich.format = Snippet::RichText;
    rich.content = "Some **bold** and `code`\n\n- item";

    Snippet code;
    code.title = "Config";
    code.language = "INI Files";
    code.content = "[section]\nkey = value -- with dashes";
    code.tags = {"config"};

    Snippet unicode;
    unicode.title = "Ünïcödé ✓";
    unicode.content = "日本語 — émoji 🎉";
    return {plain, rich, code, unicode};
}

static void compareSnippets(const QList<Snippet> &actual, const QList<Snippet> &expected, bool checkDates)
{
    QCOMPARE(actual.size(), expected.size());
    for (int i = 0; i < expected.size(); ++i) {
        QCOMPARE(actual[i].title, expected[i].title);
        QCOMPARE(actual[i].content, expected[i].content);
        QCOMPARE(actual[i].format, expected[i].format);
        QCOMPARE(actual[i].language, expected[i].language);
        QCOMPARE(actual[i].tags, expected[i].tags);
        if (checkDates) {
            QCOMPARE(actual[i].created, expected[i].created);
            QCOMPARE(actual[i].modified, expected[i].modified);
        }
    }
}

class TestImportExport : public QObject
{
    Q_OBJECT

private slots:
    void jsonRoundTrip()
    {
        QString error;
        compareSnippets(fromJson(toJson(sampleSnippets()), &error), sampleSnippets(), true);
        QVERIFY(error.isEmpty());
    }

    void markdownRoundTrip()
    {
        QString error;
        compareSnippets(fromMarkdown(toMarkdown(sampleSnippets()), &error), sampleSnippets(), true);
    }

    void csvRoundTrip()
    {
        QString error;
        compareSnippets(fromCsv(toCsv(sampleSnippets()), &error), sampleSnippets(), true);
    }

    void csvFromOtherTools()
    {
        // Semicolons, a byte order mark, Windows line endings, other column names,
        // a quoted multi-line value and a language given as the type
        QString csv = QString(QChar(0xFEFF)) + "Name;Text;Category;Type\r\n"
                      "Greeting;Hello;personal;\r\n"
                      "\"Multi; line\";\"first\r\nsecond\";work, misc;python\r\n"
                      ";;;\r\n";
        QString error;
        QList<Snippet> snippets = fromCsv(csv, &error);
        QCOMPARE(snippets.size(), 2);
        QCOMPARE(snippets[0].title, QString("Greeting"));
        QCOMPARE(snippets[0].content, QString("Hello"));
        QCOMPARE(snippets[0].tags, QStringList({"personal"}));
        QCOMPARE(snippets[1].title, QString("Multi; line"));
        QCOMPARE(snippets[1].content, QString("first\r\nsecond"));
        QCOMPARE(snippets[1].tags, QStringList({"work", "misc"}));
        QCOMPARE(snippets[1].language, QString("Python"));
    }

    void csvWithoutHeader()
    {
        QList<Snippet> snippets = fromCsv("sig,\"Best regards,\nMe\"\naddr,1 Main St\n");
        QCOMPARE(snippets.size(), 2);
        QCOMPARE(snippets[0].title, QString("sig"));
        QCOMPARE(snippets[0].content, QString("Best regards,\nMe"));

        // A single column is content; the title comes from its first line
        snippets = fromCsv("just some text\nmore text\n");
        QCOMPARE(snippets.size(), 2);
        QCOMPARE(snippets[0].title, QString("just some text"));
    }

    void jsonFromOtherTools()
    {
        QList<Snippet> snippets = fromJson(R"([{"name": "A", "body": "text a", "tags": "x; y", "lang": "sh"},
                                               {"snippet": "no title here"}, 42, {}])");
        QCOMPARE(snippets.size(), 2);
        QCOMPARE(snippets[0].title, QString("A"));
        QCOMPARE(snippets[0].content, QString("text a"));
        QCOMPARE(snippets[0].tags, QStringList({"x", "y"}));
        QCOMPARE(snippets[0].language, QString("Bash"));
        QCOMPARE(snippets[1].title, QString("no title here"));

        QString error;
        QVERIFY(fromJson("{not json", &error).isEmpty());
        QVERIFY(!error.isEmpty());
        QVERIFY(fromJson(R"({"something": "else"})", &error).isEmpty());
    }

    void handWrittenMarkdown()
    {
        QString markdown = "# First\n\nSome text\nover two lines.\n\n# Second\n\n```python\nprint('hi')\n```\n";
        QList<Snippet> snippets = fromMarkdown(markdown);
        QCOMPARE(snippets.size(), 2);
        QCOMPARE(snippets[0].title, QString("First"));
        QCOMPARE(snippets[0].content, QString("Some text\nover two lines."));
        QCOMPARE(snippets[1].content, QString("print('hi')"));
        QCOMPARE(snippets[1].language, QString("Python"));

        QString error;
        QVERIFY(fromMarkdown("no headings at all", &error).isEmpty());
        QVERIFY(!error.isEmpty());
    }

    void writesPrivateFilesAndReadsThemBack()
    {
        QTemporaryDir dir;
        for (const QString &name : {QString("out.json"), QString("out.md"), QString("out.csv")}) {
            QString path = dir.filePath(name);
            QString error;
            QVERIFY2(writeFile(path, sampleSnippets(), &error), qPrintable(error));
            QCOMPARE(int(QFile::permissions(path)) & 0x0FFF, 0x0600);
            compareSnippets(readFile(path, &error), sampleSnippets(), true);
        }
        QString error;
        QVERIFY(!writeFile(dir.filePath("out.xyz"), sampleSnippets(), &error));
    }

    void importSkipsDuplicates()
    {
        QSettings().clear();
        QTemporaryDir dir;
        Database &db = Database::instance();
        QVERIFY(db.createDatabase(dir.filePath("import.db")));
        db.addSnippet("Plain, with \"quotes\"", sampleSnippets()[0].content);

        QList<Snippet> incoming = sampleSnippets();
        incoming.append(sampleSnippets()[1]);  // the same snippet twice in one file
        int added = 0, skipped = 0;
        QString error;
        QVERIFY2(db.importSnippets(incoming, &added, &skipped, &error), qPrintable(error));
        QCOMPARE(added, 3);
        QCOMPARE(skipped, 2);
        QCOMPARE(db.snippets().size(), 4);

        // Imported snippets keep their type, tags and dates
        SnippetFilter filter;
        filter.search = "Config";
        Snippet config = db.snippets(filter).value(0);
        QCOMPARE(config.language, QString("INI Files"));
        QCOMPARE(config.tags, QStringList({"config"}));
    }
};

QTEST_MAIN(TestImportExport)
#include "tst_importexport.moc"
