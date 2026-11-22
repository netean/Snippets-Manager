#include "database.h"
#include "mainwindow.h"
#include "autostart.h"
#include "richtext.h"
#include "singleinstance.h"
#include <QSignalSpy>
#include <QStandardPaths>
#include <QMimeData>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QLineEdit>
#include <QListView>
#include <QMessageBox>
#include <QInputDialog>
#include <QAction>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QTextEdit>
#include <QTextLayout>
#include <QTextBlock>
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

    void deleteMovesToTrashAndUndoes()
    {
        Database::instance().addSnippet("Doomed", "x");
        Database::instance().addSnippet("Other", "y");
        reload();
        selectRow(0);
        QString title = m_title->text();

        // Delete doesn't ask: it can be undone
        m_list->setFocus();
        QTest::keyClick(m_list, Qt::Key_Delete);
        QCOMPARE(Database::instance().snippets().size(), 1);
        QCOMPARE(Database::instance().trashCount(), 1);
        QCOMPARE(m_list->model()->rowCount(), 1);
        QVERIFY(m_tagFilter->findText("Trash (1)") >= 0);

        // Ctrl+Z (outside a text field) brings it back
        m_list->setFocus();
        QTest::keyClick(m_window, Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(Database::instance().snippets().size(), 2);
        QCOMPARE(Database::instance().trashCount(), 0);
        QCOMPARE(m_title->text(), title);
    }

    void trashViewRestoresAndDeletesForever()
    {
        int a = Database::instance().addSnippet("A", "a");
        int b = Database::instance().addSnippet("B", "b");
        Database::instance().moveToTrash(a);
        Database::instance().moveToTrash(b);
        reload();
        m_tagFilter->setCurrentIndex(m_tagFilter->findText("Trash (2)"));
        QCOMPARE(m_list->model()->rowCount(), 2);

        // Snippets in the trash can't be edited
        selectRow(0);
        QVERIFY(m_title->isReadOnly());
        QVERIFY(m_content->isReadOnly());

        m_window->findChild<QPushButton *>("restoreButton")->click();
        QCOMPARE(Database::instance().snippets().size(), 1);
        QCOMPARE(m_list->model()->rowCount(), 1);

        // Deleting forever asks first
        selectRow(0);
        answerNextQuestion(QMessageBox::No);
        m_window->findChild<QPushButton *>("deleteButton")->click();
        QCOMPARE(Database::instance().trashCount(), 1);
        answerNextQuestion(QMessageBox::Yes);
        m_window->findChild<QPushButton *>("deleteButton")->click();
        QCOMPARE(Database::instance().trashCount(), 0);
        QCOMPARE(m_list->model()->rowCount(), 0);
    }

    void richTextFormattingIsSavedAsMarkdown()
    {
        Database::instance().addSnippet("Note", "");
        reload();
        selectRow(0);
        QTest::keyClick(m_window, Qt::Key_E, Qt::ControlModifier);
        m_type->setCurrentIndex(m_type->findData("rich"));
        QVERIFY(m_window->findChild<QWidget *>("formatBar")->isVisible());

        m_content->setFocus();
        QTest::keyClicks(m_content, "plain ");
        QTest::keyClick(m_content, Qt::Key_B, Qt::ControlModifier);
        QTest::keyClicks(m_content, "bold");
        QTest::keyClick(m_window, Qt::Key_S, Qt::ControlModifier);

        Snippet saved = Database::instance().snippets().value(0);
        QCOMPARE(saved.format, Snippet::RichText);
        QCOMPARE(saved.content, QString("plain **bold**"));

        // Copying gives formatted and plain versions
        QTest::keyClick(m_window, Qt::Key_C, Qt::ControlModifier | Qt::ShiftModifier);
        const QMimeData *copied = QApplication::clipboard()->mimeData();
        QCOMPARE(copied->text(), QString("plain bold"));
        QVERIFY(copied->hasHtml());
        QVERIFY(copied->html().contains("font-weight"));
    }

    void plainTextModeShowsMarkdownAndKeepsIt()
    {
        Snippet rich;
        rich.title = "Rich";
        rich.format = Snippet::RichText;
        rich.content = "- one\n- **two**";
        Database::instance().addSnippet(rich);
        QSettings().setValue("editor/richText", false);
        reload();
        selectRow(0);
        QCOMPARE(m_content->toPlainText(), rich.content);
        QCOMPARE(QApplication::clipboard()->text(), rich.content);

        // Editing the title doesn't change the content or its type
        m_title->setText("Renamed");
        QTest::keyClick(m_window, Qt::Key_S, Qt::ControlModifier);
        Snippet saved = Database::instance().snippets().value(0);
        QCOMPARE(saved.title, QString("Renamed"));
        QCOMPARE(saved.format, Snippet::RichText);
        QCOMPARE(saved.content, rich.content);
        QVERIFY(!m_window->findChild<QWidget *>("formatBar")->isVisible());
    }

    void removingFormattingAsksFirst()
    {
        Snippet rich;
        rich.title = "Rich";
        rich.format = Snippet::RichText;
        rich.content = "**important**";
        Database::instance().addSnippet(rich);
        reload();
        selectRow(0);
        QTest::keyClick(m_window, Qt::Key_E, Qt::ControlModifier);

        answerNextQuestion(QMessageBox::No);
        m_type->setCurrentIndex(m_type->findData("plain"));
        QCOMPARE(m_type->currentData().toString(), QString("rich"));

        answerNextQuestion(QMessageBox::Yes);
        m_type->setCurrentIndex(m_type->findData("plain"));
        clickSave();  // the offscreen platform doesn't reactivate the window for Ctrl+S after a dialog
        Snippet saved = Database::instance().snippets().value(0);
        QCOMPARE(saved.format, Snippet::PlainText);
        QCOMPARE(saved.content, QString("important"));
    }

    void plainTextBecomesRichTextLiterally()
    {
        Database::instance().addSnippet("Stars", "*not italic* # not a heading");
        reload();
        selectRow(0);
        QTest::keyClick(m_window, Qt::Key_E, Qt::ControlModifier);
        m_type->setCurrentIndex(m_type->findData("rich"));
        QCOMPARE(m_content->toPlainText(), QString("*not italic* # not a heading"));
        QTest::keyClick(m_window, Qt::Key_S, Qt::ControlModifier);
        Snippet saved = Database::instance().snippets().value(0);
        QCOMPARE(saved.format, Snippet::RichText);
        QCOMPARE(RichText::toPlainText(saved.content), QString("*not italic* # not a heading"));
    }

    void codeLanguageIsSaved()
    {
        Database::instance().addSnippet("Script", "print('hi')");
        reload();
        selectRow(0);
        QTest::keyClick(m_window, Qt::Key_E, Qt::ControlModifier);
        m_type->setCurrentIndex(m_type->findData("code:Python"));
        QTest::keyClick(m_window, Qt::Key_S, Qt::ControlModifier);
        Snippet saved = Database::instance().snippets().value(0);
        QCOMPARE(saved.format, Snippet::PlainText);
        QCOMPARE(saved.language, QString("Python"));
        QCOMPARE(saved.content, QString("print('hi')"));
    }

    void highlightsCodeWhenEnabled()
    {
#ifndef HAVE_SYNTAX_HIGHLIGHTING
        QSKIP("Built without KSyntaxHighlighting");
#else
        Snippet code;
        code.title = "Code";
        code.language = "Python";
        code.content = "def hello():\n    return 'hi'  # comment";
        Database::instance().addSnippet(code);
        reload();
        selectRow(0);
        QVERIFY(!m_content->document()->firstBlock().layout()->formats().isEmpty());
        QCOMPARE(m_content->toPlainText(), code.content);

        // Highlighting doesn't count as a change
        QVERIFY(!m_window->findChild<QPushButton *>("saveButton")->isEnabled());

        // Switching between code and other snippets doesn't count as a change either
        Database::instance().addSnippet("Plain", "text");
        reload();
        selectRow(0);
        selectRow(1);
        selectRow(0);
        QVERIFY(!m_window->findChild<QPushButton *>("saveButton")->isEnabled());

        QSettings().setValue("editor/syntaxHighlighting", false);
        reload();
        selectRow(0);
        QVERIFY(m_content->document()->firstBlock().layout()->formats().isEmpty());
#endif
    }

    void copyFillsInPlaceholders()
    {
        Database::instance().addSnippet("Dated", "Sent {{date}}");
        reload();
        selectRow(0);
        QString today = QDate::currentDate().toString("yyyy-MM-dd");
        QCOMPARE(QApplication::clipboard()->text(), "Sent " + today);
        QCOMPARE(m_content->toPlainText(), QString("Sent {{date}}"));
        QCOMPARE(Database::instance().snippets().value(0).content, QString("Sent {{date}}"));

        QSettings().setValue("clipboard/placeholders", false);
        QTest::keyClick(m_window, Qt::Key_C, Qt::ControlModifier | Qt::ShiftModifier);
        QCOMPARE(QApplication::clipboard()->text(), QString("Sent {{date}}"));
    }

    void quickSearchCopiesAndOpens()
    {
        Database::instance().addSnippet("Address", "1 Main Street");
        Database::instance().addSnippet("Phone", "555 {{date:yyyy}}");
        reload();
        m_window->handleCommand("popup");
        QWidget *popup = nullptr;
        for (QWidget *widget : QApplication::topLevelWidgets()) {
            if (widget->objectName() == "quickSearch") popup = widget;
        }
        QVERIFY(popup);
        QVERIFY(popup->isVisible());
        auto *search = popup->findChild<QLineEdit *>("quickSearchEdit");
        auto *results = popup->findChild<QListView *>("quickSearchList");
        QCOMPARE(results->model()->rowCount(), 2);

        // Typing filters; Enter copies (filling in placeholders) and closes
        QTest::keyClicks(search, "phone");
        QCOMPARE(results->model()->rowCount(), 1);
        QTest::keyClick(search, Qt::Key_Return);
        QCOMPARE(QApplication::clipboard()->text(), "555 " + QString::number(QDate::currentDate().year()));
        QVERIFY(!popup->isVisible());

        // Ctrl+Enter shows the snippet in the main window instead
        m_window->handleCommand("popup");
        QTest::keyClicks(search, "address");
        QTest::keyClick(search, Qt::Key_Return, Qt::ControlModifier);
        QVERIFY(!popup->isVisible());
        QCOMPARE(m_title->text(), QString("Address"));
    }

    void commandsReachTheRunningCopy()
    {
        QVERIFY(!SingleInstance::sendToRunning("show"));
        SingleInstance instance;
        QVERIFY(instance.listen());
        QSignalSpy received(&instance, &SingleInstance::commandReceived);
        QVERIFY(SingleInstance::sendToRunning("popup token-123"));
        QVERIFY(received.wait(2000));
        QCOMPARE(received.first().first().toString(), QString("popup token-123"));
    }

    void startAtLoginWritesDesktopFile()
    {
#if defined(Q_OS_LINUX)
        QVERIFY(Autostart::setEnabled(true));
        QVERIFY(Autostart::isEnabled());
        QFile file(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
                   + "/autostart/io.github.netean.SnippetManager.desktop");
        QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY(file.readAll().contains(" --background\n"));
        QVERIFY(Autostart::setEnabled(false));
        QVERIFY(!Autostart::isEnabled());
#endif
        QCOMPARE(Autostart::desktopExecQuote("/usr/bin/snippetmanager"), QString("/usr/bin/snippetmanager"));
        QCOMPARE(Autostart::desktopExecQuote("/opt/My Apps/app"), QString("\"/opt/My Apps/app\""));
        QCOMPARE(Autostart::desktopExecQuote("/x/a\"b$c"), QString("\"/x/a\\\\\"b\\\\$c\""));
    }

    void foldersHoldSnippets()
    {
        Database::instance().addSnippet("Loose", "x");
        reload();

        // New Folder asks for a name
        answerNextInput("Work");
        m_window->findChild<QAction *>("newFolderAction")->trigger();
        QCOMPARE(Database::instance().folders().size(), 1);
        int work = Database::instance().folders().value(0).id;
        QAbstractItemModel *model = m_list->model();
        QCOMPARE(model->rowCount(), 2);  // the folder, then the snippet
        QModelIndex folderIndex = model->index(0, 0);
        QCOMPARE(folderIndex.data().toString(), QString("Work"));

        // With the folder selected, a new snippet goes in it
        m_list->setCurrentIndex(folderIndex);
        // (Triggered directly: after a dialog the offscreen platform leaves no active window for Ctrl+N)
        m_window->findChild<QAction *>("newAction")->trigger();
        SnippetFilter filter;
        filter.search = "New Snippet";
        Snippet created = Database::instance().snippets(filter).value(0);
        QCOMPARE(created.folderId, work);
        QCOMPARE(model->rowCount(model->index(0, 0)), 1);

        // Dragging "Loose" onto the folder moves it there
        QModelIndex loose;
        for (int row = 0; row < model->rowCount(); ++row) {
            if (model->index(row, 0).data().toString() == "Loose") loose = model->index(row, 0);
        }
        QVERIFY(loose.isValid());
        QMimeData *dragged = model->mimeData({loose});
        QVERIFY(model->dropMimeData(dragged, Qt::MoveAction, -1, 0, model->index(0, 0)));
        delete dragged;
        QTRY_COMPARE(model->rowCount(model->index(0, 0)), 2);
        QCOMPARE(model->rowCount(), 1);

        // Searching shows a flat list, without folders
        QLineEdit *search = m_window->findChild<QLineEdit *>("searchEdit");
        search->setText("Loose");
        QTRY_COMPARE(model->index(0, 0).data().toString(), QString("Loose"));
        QCOMPARE(model->rowCount(), 1);
        search->clear();
        QTRY_COMPARE(model->index(0, 0).data().toString(), QString("Work"));

        // Deleting the folder keeps its snippets, at the top level
        m_list->setCurrentIndex(model->index(0, 0));
        m_window->findChild<QPushButton *>("deleteButton")->click();
        QVERIFY(Database::instance().folders().isEmpty());
        QCOMPARE(Database::instance().snippets().size(), 2);
        QCOMPARE(model->rowCount(), 2);
    }

private:
    // Types into the next input dialog that opens and accepts it
    static void answerNextInput(const QString &text)
    {
        QTimer::singleShot(0, [text]() {
            auto *dialog = qobject_cast<QInputDialog *>(QApplication::activeModalWidget());
            QVERIFY2(dialog, "Expected an input dialog");
            dialog->setTextValue(text);
            dialog->accept();
        });
    }

    void createWindow()
    {
        m_window = new MainWindow;
        m_window->show();
        QVERIFY(QTest::qWaitForWindowExposed(m_window));
        m_list = m_window->findChild<QAbstractItemView *>("snippetList");
        m_title = m_window->findChild<QLineEdit *>("titleEdit");
        m_tags = m_window->findChild<QLineEdit *>("tagEdit");
        m_content = m_window->findChild<QTextEdit *>("contentEdit");
        m_tagFilter = m_window->findChild<QComboBox *>("tagFilter");
        m_type = m_window->findChild<QComboBox *>("typeCombo");
        QVERIFY(m_list && m_title && m_tags && m_content && m_tagFilter && m_type);
    }

    void clickSave()
    {
        QPushButton *saveButton = m_window->findChild<QPushButton *>("saveButton");
        QVERIFY(saveButton);
        saveButton->click();
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
    QAbstractItemView *m_list = nullptr;
    QLineEdit *m_title = nullptr;
    QLineEdit *m_tags = nullptr;
    QTextEdit *m_content = nullptr;
    QComboBox *m_tagFilter = nullptr;
    QComboBox *m_type = nullptr;
};

QTEST_MAIN(TestMainWindow)
#include "tst_mainwindow.moc"
