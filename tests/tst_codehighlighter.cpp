#include "testenv.h"
#include "codehighlighter.h"
#include <QPalette>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextLayout>
#include <QtTest>

// The colours the highlighter uses on a light background
static const QColor Keyword("#8e24aa"), String("#2e7d32"), Comment("#78909c"), Number("#d84315"),
    Variable("#c62828"), Key("#1565c0"), Preprocessor("#00838f"), Function("#6d4c41");

class TestCodeHighlighter : public QObject
{
    Q_OBJECT

private:
    // The colour given to the text at a position (invalid if none)
    static QColor colourAt(const QTextDocument &document, int line, int column)
    {
        QTextBlock block = document.findBlockByNumber(line);
        for (const QTextLayout::FormatRange &range : block.layout()->formats()) {
            if (column >= range.start && column < range.start + range.length) {
                return range.format.foreground().color();
            }
        }
        return QColor();
    }

    static void highlight(QTextDocument &document, CodeHighlighter &highlighter, const QString &language,
                          const QString &text)
    {
        document.setPlainText(text);
        highlighter.setDocument(&document);
        QPalette light;
        light.setColor(QPalette::Base, Qt::white);
        highlighter.setLanguage(language, light);
    }

private slots:
    void python()
    {
        QTextDocument document;
        CodeHighlighter highlighter;
        highlight(document, highlighter, "Python",
                  "@cache\ndef go(x=42):  # note\n    return '#not a comment'\n\"\"\"doc\nstring\"\"\" + 1");
        QCOMPARE(colourAt(document, 0, 1), Preprocessor);   // @cache
        QCOMPARE(colourAt(document, 1, 0), Keyword);        // def
        QCOMPARE(colourAt(document, 1, 4), Function);       // go(
        QCOMPARE(colourAt(document, 1, 9), Number);         // 42
        QCOMPARE(colourAt(document, 1, 16), Comment);       // # note
        QCOMPARE(colourAt(document, 2, 4), Keyword);        // return
        QCOMPARE(colourAt(document, 2, 12), String);        // '#not a comment'
        QCOMPARE(colourAt(document, 3, 3), String);         // """doc
        QCOMPARE(colourAt(document, 4, 2), String);         // string""" (continued)
        QCOMPARE(colourAt(document, 4, 12), Number);        // 1, after the string ends
    }

    void cBlockCommentsAndPreprocessor()
    {
        QTextDocument document;
        CodeHighlighter highlighter;
        highlight(document, highlighter, "C++", "#include <x>\n/* start\nend */ int y = \"s\";");
        QCOMPARE(colourAt(document, 0, 3), Preprocessor);
        QCOMPARE(colourAt(document, 1, 3), Comment);
        QCOMPARE(colourAt(document, 2, 1), Comment);
        QVERIFY(colourAt(document, 2, 8).isValid());        // int: a type
        QCOMPARE(colourAt(document, 2, 16), String);
    }

    void shellVariablesAndComments()
    {
        QTextDocument document;
        CodeHighlighter highlighter;
        highlight(document, highlighter, "Bash", "echo \"$HOME\" $# ${USER} # done");
        QCOMPARE(colourAt(document, 0, 6), String);         // "$HOME" is in a string
        QCOMPARE(colourAt(document, 0, 13), Variable);      // $# isn't a comment
        QCOMPARE(colourAt(document, 0, 16), Variable);      // ${USER}
        QCOMPARE(colourAt(document, 0, 26), Comment);       // # done
    }

    void jsonKeysAndValues()
    {
        QTextDocument document;
        CodeHighlighter highlighter;
        highlight(document, highlighter, "JSON", "{\"name\": \"value\", \"n\": 3, \"ok\": true}");
        QCOMPARE(colourAt(document, 0, 2), Key);
        QCOMPARE(colourAt(document, 0, 11), String);
        QCOMPARE(colourAt(document, 0, 23), Number);
    }

    void markupAndOthers()
    {
        QTextDocument document;
        CodeHighlighter highlighter;
        highlight(document, highlighter, "HTML", "<a href=\"x\"><!-- note --></a>");
        QVERIFY(colourAt(document, 0, 1).isValid());        // tag
        QCOMPARE(colourAt(document, 0, 9), String);
        QCOMPARE(colourAt(document, 0, 15), Comment);

        highlight(document, highlighter, "Diff", "+added\n-removed\n context");
        QVERIFY(document.findBlockByNumber(0).layout()->formats().value(0).format.background().style() != Qt::NoBrush);
        QVERIFY(!colourAt(document, 2, 2).isValid());
    }

    void everyListedLanguageIsKnown()
    {
        const QStringList languages = {"Bash", "C", "C#", "C++", "CMake", "CSS", "Diff", "Dockerfile", "Fish", "Go",
                                        "HTML", "INI Files", "Java", "JavaScript", "JSON", "Kotlin", "Lua", "Makefile",
                                        "Markdown", "Perl", "PHP", "PowerShell", "Python", "Ruby", "Rust", "SQL",
                                        "Swift", "TOML", "TypeScript", "XML", "YAML", "Zsh"};
        for (const QString &language : languages) {
            QVERIFY2(CodeHighlighter::supports(language), qPrintable(language));
        }
        QVERIFY(!CodeHighlighter::supports("Klingon"));

        // Unknown languages are left uncoloured
        QTextDocument document;
        CodeHighlighter highlighter;
        highlight(document, highlighter, "Klingon", "def x = 1 # y");
        QVERIFY(document.firstBlock().layout()->formats().isEmpty());
    }
};

QTEST_MAIN(TestCodeHighlighter)
#include "tst_codehighlighter.moc"
