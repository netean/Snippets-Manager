#include "testenv.h"
#include "placeholders.h"
#include "richtext.h"
#include <QtTest>

class TestPlaceholders : public QObject
{
    Q_OBJECT

private:
    static Placeholders::Context context()
    {
        Placeholders::Context context;
        context.now = QDateTime(QDate(2026, 11, 22), QTime(9, 5, 7));
        context.clipboard = "from *clipboard*";
        context.uuid = []() { return QString("1234-abcd"); };
        return context;
    }

private slots:
    void expands_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<QString>("expected");
        QTest::newRow("date") << "Today is {{date}}." << "Today is 2026-11-22.";
        QTest::newRow("time") << "{{time}}" << "09:05";
        QTest::newRow("datetime") << "{{datetime}}" << "2026-11-22 09:05";
        QTest::newRow("formats") << "{{date:dd/MM/yyyy}} {{time:h:mm:ss ap}}" << "22/11/2026 9:05:07 am";
        QTest::newRow("case and spaces") << "{{ DATE }} {{Clipboard}}" << "2026-11-22 from *clipboard*";
        QTest::newRow("uuid") << "id={{uuid}}" << "id=1234-abcd";
        QTest::newRow("unknown names") << "{{ item }} {{name}} ${HOME} {date}" << "{{ item }} {{name}} ${HOME} {date}";
        QTest::newRow("escaped") << "\\{{date}} is {{date}}" << "{{date}} is 2026-11-22";
        QTest::newRow("several") << "{{date}}{{date}}" << "2026-11-222026-11-22";
        QTest::newRow("no placeholders") << "plain text" << "plain text";
        QTest::newRow("unclosed") << "{{date" << "{{date";
    }

    void expands()
    {
        QFETCH(QString, text);
        QFETCH(QString, expected);
        QCOMPARE(Placeholders::expand(text, context()), expected);
    }

    void escapesValuesInRichText()
    {
        // The clipboard text appears as written, not as italics
        QString markdown = Placeholders::expand("Pasted: {{clipboard}}", context(), true);
        QCOMPARE(RichText::toPlainText(markdown), QString("Pasted: from *clipboard*"));
    }
};

QTEST_MAIN(TestPlaceholders)
#include "tst_placeholders.moc"
