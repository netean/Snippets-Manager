#include "database.h"
#include "mainwindow.h"
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QLineEdit>
#include <QListView>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QTextEdit>
#include <QTimer>
#include <QtTest>

// Drives the main window the way a user would, with keyboard shortcuts
class TestMainWindow : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        QSettings().clear();
        QVERIFY(m_dir.isValid());
        QVERIFY(Database::instance().createDatabase(m_dir.filePath(QString("gui-%1.db").arg(++m_count))));
        createWindow();
    }

    void cleanup()
    {
        delete m_window;
        m_window = nullptr;
    }

    void createRenameTagAndSave()
    {
        QTest::keyClick(m_window, Qt::Key_N, Qt::ControlModifier);
        QCOMPARE(m_list->model()->rowCount(), 1);
        QCOMPARE(QApplication::focusWidget(), m_title);
        QCOMPARE(m_title->selectedText(), QString("New Snippet"));

        QTest::keyClicks(m_title, "Greeting");
        m_tags->setFocus();
        QTest::keyClicks(m_tags, "work, Personal");
        m_content->setFocus();
        QTest::keyClicks(m_content, "Hello there");
        QTest::keyClick(m_window, Qt::Key_S, Qt::ControlModifier);

        Snippet saved = Database::instance().snippets().value(0);
        QCOMPARE(saved.title, QString("Greeting"));
        QCOMPARE(saved.content, QString("Hello there"));
        QCOMPARE(saved.tags, QStringList({"Personal", "work"}));
        QCOMPARE(m_tags->text(), QString("Personal, work"));

        // The tag filter lists the new tags
        QCOMPARE(m_tagFilter->findText("work (1)") >= 0, true);
    }

    void savingKeepsCursorWhileEditing()
    {
        Database::instance().addSnippet("One", "first line");
        reload();
        selectRow(0);
        QTest::keyClick(m_window, Qt::Key_E, Qt::ControlModifier);
        QCOMPARE(QApplication::focusWidget(), m_content);
        QTextCursor cursor = m_content->textCursor();
        cursor.movePosition(QTextCursor::End);
        m_content->setTextCursor(cursor);
        QTest::keyClicks(m_content, " more");
        QTest::keyClick(m_window, Qt::Key_S, Qt::ControlModifier);
        QCOMPARE(Database::instance().snippets().value(0).content, QString("first line more"));
        QCOMPARE(m_content->textCursor().position(), m_content->toPlainText().size());
    }

    void switchingSnippetsSavesChanges()
    {
        Database::instance().addSnippet("A", "a");
        Database::instance().addSnippet("B", "b");
        reload();
        selectRow(0);
        QString firstTitle = m_title->text();
        m_title->setText(firstTitle + " renamed");
        selectRow(1);
        QStringList titles;
        for (const Snippet &snippet : Database::instance().snippets()) {
            titles << snippet.title;
        }
        QVERIFY(titles.contains(firstTitle + " renamed"));
    }

    void selectingCopiesToClipboard()
    {
        Database::instance().addSnippet("Copy me", "clipboard text");
        reload();
        selectRow(0);
        QCOMPARE(QApplication::clipboard()->text(), QString("clipboard text"));
    }

    void filtersByTag()
    {
        Snippet tagged;
        tagged.title = "tagged";
        tagged.tags = {"work"};
        Database::instance().addSnippet(tagged);
        Database::instance().addSnippet("plain", "");
        reload();
        QCOMPARE(m_list->model()->rowCount(), 2);
        m_tagFilter->setCurrentIndex(m_tagFilter->findText("work (1)"));
        QCOMPARE(m_list->model()->rowCount(), 1);
        m_tagFilter->setCurrentIndex(m_tagFilter->findText("Untagged"));
        QCOMPARE(m_list->model()->rowCount(), 1);

        // New snippets get the tag being shown
        m_tagFilter->setCurrentIndex(m_tagFilter->findText("work (1)"));
        QTest::keyClick(m_window, Qt::Key_N, Qt::ControlModifier);
        QCOMPARE(m_list->model()->rowCount(), 2);
    }

    void deleteAsksFirst()
    {
        Database::instance().addSnippet("Doomed", "x");
        reload();
        selectRow(0);

        // The Delete key in the list asks first; answering No keeps the snippet
        answerNextQuestion(QMessageBox::No);
        m_list->setFocus();
        QTest::keyClick(m_list, Qt::Key_Delete);
        QCOMPARE(Database::instance().snippets().size(), 1);

        // The Delete button asks too; answering Yes deletes it
        answerNextQuestion(QMessageBox::Yes);
        QPushButton *deleteButton = m_window->findChild<QPushButton *>("deleteButton");
        QVERIFY(deleteButton);
        deleteButton->click();
        QCOMPARE(Database::instance().snippets().size(), 0);
        QCOMPARE(m_list->model()->rowCount(), 0);
    }

private:
    void createWindow()
    {
        m_window = new MainWindow;
        m_window->show();
        QVERIFY(QTest::qWaitForWindowExposed(m_window));
        m_list = m_window->findChild<QListView *>("snippetList");
        m_title = m_window->findChild<QLineEdit *>("titleEdit");
        m_tags = m_window->findChild<QLineEdit *>("tagEdit");
        m_content = m_window->findChild<QTextEdit *>("contentEdit");
        m_tagFilter = m_window->findChild<QComboBox *>("tagFilter");
        QVERIFY(m_list && m_title && m_tags && m_content && m_tagFilter);
    }

    // Recreates the window so it shows snippets added directly to the database
    void reload()
    {
        cleanup();
        createWindow();
    }

    void selectRow(int row)
    {
        m_list->setCurrentIndex(m_list->model()->index(row, 0));
    }

    // Clicks a button in the next message box that opens
    static void answerNextQuestion(QMessageBox::StandardButton button)
    {
        QTimer::singleShot(0, [button]() {
            auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            QVERIFY2(box, "Expected a question to be asked");
            box->button(button)->click();
        });
    }

    QTemporaryDir m_dir;
    int m_count = 0;
    MainWindow *m_window = nullptr;
    QListView *m_list = nullptr;
    QLineEdit *m_title = nullptr;
    QLineEdit *m_tags = nullptr;
    QTextEdit *m_content = nullptr;
    QComboBox *m_tagFilter = nullptr;
};

QTEST_MAIN(TestMainWindow)
#include "tst_mainwindow.moc"
