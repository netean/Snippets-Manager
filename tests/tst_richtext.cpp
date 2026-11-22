#include "richtext.h"
#include <QTextCursor>
#include <QTextDocument>
#include <QTextList>
#include <QtTest>

class TestRichText : public QObject
{
    Q_OBJECT

private slots:
    void plainTextSurvivesRoundTrip_data()
    {
        QTest::addColumn<QString>("text");
        QTest::newRow("emphasis characters") << "2*3*4 and snake_case_name";
        QTest::newRow("brackets and tags") << "[not a link](x) <b>not bold</b> & &amp;";
        QTest::newRow("code and strike") << "`not code` ~~not struck~~ back\\slash";
        QTest::newRow("line starts") << "# not heading\n- not list\n+ nor this\n1. not numbered\n2) nor this\n> not quote\n=== not underline";
        QTest::newRow("pipes") << "| a | b |\n|---|---|";
        QTest::newRow("several lines") << "first line\nsecond line\nthird";
        QTest::newRow("unicode") << "Ünïcödé ✓ 日本語 *ok*";
    }

    void plainTextSurvivesRoundTrip()
    {
        QFETCH(QString, text);
        QString markdown = RichText::fromPlainText(text);
        QCOMPARE(RichText::toPlainText(markdown), text);
    }

    void writesFormatting()
    {
        QTextDocument document;
        QTextCursor cursor(&document);
        cursor.insertText("plain ");
        QTextCharFormat bold;
        bold.setFontWeight(QFont::Bold);
        cursor.insertText("bold", bold);
        cursor.insertText(" ", QTextCharFormat());
        QTextCharFormat italic;
        italic.setFontItalic(true);
        cursor.insertText("italic ", italic);  // trailing space moves outside the markers
        QTextCharFormat strike;
        strike.setFontStrikeOut(true);
        cursor.insertText("gone", strike);
        cursor.insertText(" ", QTextCharFormat());
        QTextCharFormat code;
        code.setFontFixedPitch(true);
        cursor.insertText("a`b", code);
        QCOMPARE(RichText::toMarkdown(&document), QString("plain **bold** *italic* ~~gone~~ ``a`b``"));
    }

    void writesLists()
    {
        QTextDocument document;
        QTextCursor cursor(&document);
        cursor.insertText("Intro");
        cursor.insertBlock();
        cursor.createList(QTextListFormat::ListDisc);
        cursor.insertText("one");
        cursor.insertBlock();
        QTextCharFormat bold;
        bold.setFontWeight(QFont::Bold);
        cursor.insertText("two", bold);
        cursor.insertBlock(QTextBlockFormat(), QTextCharFormat());
        cursor.createList(QTextListFormat::ListDecimal);
        cursor.insertText("first");
        cursor.insertBlock();
        cursor.insertText("second");

        QString markdown = RichText::toMarkdown(&document);
        QCOMPARE(markdown, QString("Intro\n\n- one\n- **two**\n\n1. first\n2. second"));

        // Reading it back gives the same text and lists
        QTextDocument read;
        read.setMarkdown(markdown);
        QCOMPARE(read.toPlainText(), document.toPlainText());
        QVERIFY(read.findBlockByNumber(1).textList());
        QCOMPARE(read.findBlockByNumber(4).textList()->format().style(), QTextListFormat::ListDecimal);
    }

    void formattingSurvivesRoundTrip()
    {
        QString markdown = "Some **bold**, *italic*, ~~struck~~ and `code` with [a link](https://example.com/a%20b)\n\n- item **one**\n- item 2\\*3";
        QTextDocument document;
        document.setMarkdown(markdown);
        QCOMPARE(RichText::toMarkdown(&document), markdown);
    }

    void dropsImages()
    {
        QTextDocument document;
        document.setHtml("before <img src=\"data:image/png;base64,\"> after");
        QCOMPARE(RichText::toMarkdown(&document), QString("before  after"));
    }

    void dropsEmptyParagraphs()
    {
        QCOMPARE(RichText::fromPlainText("a\n\n\nb"), QString("a\n\nb"));
        QCOMPARE(RichText::fromPlainText(""), QString(""));
    }
};

QTEST_MAIN(TestRichText)
#include "tst_richtext.moc"
