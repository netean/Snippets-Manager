#include "mainwindow.h"
#include "clipboard.h"
#include "database.h"
#include "globalshortcut.h"
#include "importexport.h"
#include "quicksearch.h"
#include "messagebox.h"
#include "settingsdialog.h"
#include "snippeteditor.h"
#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QStandardPaths>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QLocale>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QSessionManager>
#include <QSettings>
#include <QSplitter>
#include <QStatusBar>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QVBoxLayout>

static const char *SortOrderKey = "view/sortOrder";
static const char *GeometryKey = "window/geometry";
static const char *SplitterKey = "window/splitter";

// What each entry in the tag filter shows
enum TagFilterKind { AllSnippets, Untagged, OneTag, Trash };
static const int TagFilterKindRole = Qt::UserRole;
static const int TagFilterNameRole = Qt::UserRole + 1;

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setupUI();
    setupActions();
    setupMenus();

    m_model = new SnippetModel(this);
    m_snippetList->setModel(m_model);

    // Restore the last used sort order
    int savedOrder = QSettings().value(SortOrderKey, Database::CreatedDescending).toInt();
    int comboIndex = m_sortCombo->findData(savedOrder);
    m_sortCombo->setCurrentIndex(comboIndex >= 0 ? comboIndex : 0);
    m_model->setSortOrder(static_cast<Database::SortOrder>(m_sortCombo->currentData().toInt()));
    refreshTags();

    m_searchTimer = new QTimer(this);
    m_searchTimer->setSingleShot(true);
    m_searchTimer->setInterval(300);
    connect(m_searchTimer, &QTimer::timeout, this, &MainWindow::onSearchTextChanged);

    connect(m_snippetList->selectionModel(), &QItemSelectionModel::currentChanged,
            this, &MainWindow::onSelectionChanged);
    connect(m_searchEdit, &QLineEdit::textChanged, this, [this]() { m_searchTimer->start(); });
    connect(m_sortCombo, &QComboBox::currentIndexChanged, this, &MainWindow::onSortOrderChanged);
    connect(m_tagFilterCombo, &QComboBox::currentIndexChanged, this, &MainWindow::onTagFilterChanged);
    connect(m_addButton, &QPushButton::clicked, m_newAction, &QAction::trigger);
    connect(m_editButton, &QPushButton::clicked, m_editAction, &QAction::trigger);
    connect(m_deleteButton, &QPushButton::clicked, m_deleteAction, &QAction::trigger);
    connect(m_restoreButton, &QPushButton::clicked, m_restoreAction, &QAction::trigger);
    connect(m_saveButton, &QPushButton::clicked, m_saveAction, &QAction::trigger);
    connect(m_copyButton, &QPushButton::clicked, m_copyAction, &QAction::trigger);
    connect(m_editor, &SnippetEditor::changed, this, &MainWindow::onEditorChanged);
    connect(m_editor, &SnippetEditor::returnPressed, this, &MainWindow::ensureSaved);

    // Clicking in the content area copies it to the clipboard
    m_editor->contentViewport()->installEventFilter(this);

    loadSnippet(-1);

    setWindowTitle(QString("Snippet Manager %1 - Text & Code Snippets").arg(QApplication::applicationVersion()));

    QLabel *versionLabel = new QLabel("v" + QApplication::applicationVersion());
    versionLabel->setStyleSheet("color: #666; padding: 0 4px;");
    statusBar()->addPermanentWidget(versionLabel);

    // Restore the window size and the split between the list and the snippet
    QSettings settings;
    if (!restoreGeometry(settings.value(GeometryKey).toByteArray())) {
        resize(900, 600);
    }
    m_splitter->restoreState(settings.value(SplitterKey).toByteArray());

    // Qt 6 doesn't close windows when the desktop session ends (logging out or
    // shutting down), so save any edit in progress when the session manager asks
    connect(qApp, &QGuiApplication::commitDataRequest, this, [this](QSessionManager &) {
        saveCurrentSnippet();
    }, Qt::DirectConnection);

    m_quickSearch = new QuickSearch;
    connect(m_quickSearch, &QuickSearch::openRequested, this, &MainWindow::openSnippet);
    connect(m_quickSearch, &QuickSearch::copied, this, [this](const QString &title) {
        statusBar()->showMessage(QString("Copied '%1'").arg(title), 4000);
    });
    applyQuickAccessSettings();
}

MainWindow::~MainWindow()
{
    saveCurrentSnippet();
    delete m_quickSearch;
}

bool MainWindow::canRunInBackground() const
{
    return m_tray && m_tray->isVisible();
}

void MainWindow::applyQuickAccessSettings()
{
    QSettings settings;

    // Tray icon
    bool wantTray = settings.value(SettingsKeys::TrayIcon, true).toBool() && QSystemTrayIcon::isSystemTrayAvailable();
    if (wantTray && !m_tray) {
        m_tray = new QSystemTrayIcon(QApplication::windowIcon(), this);
        m_tray->setToolTip("Snippet Manager");
        QMenu *menu = new QMenu(this);
        menu->addAction("&Quick Search", this, [this]() { showQuickSearch(); });
        menu->addAction("&Show Snippet Manager", this, &MainWindow::showWindow);
        menu->addSeparator();
        menu->addAction("&Quit", this, &MainWindow::onQuit);
        m_tray->setContextMenu(menu);
        connect(m_tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
            if (reason == QSystemTrayIcon::Trigger) {
                if (isVisible() && isActiveWindow()) {
                    close();  // hides to the tray
                } else {
                    showWindow();
                }
            } else if (reason == QSystemTrayIcon::MiddleClick) {
                showQuickSearch();
            }
        });
        m_tray->show();
    } else if (!wantTray && m_tray) {
        delete m_tray;
        m_tray = nullptr;
    }
    // With a tray icon, closing the window leaves the app running
    QApplication::setQuitOnLastWindowClosed(!m_tray);

    // Global shortcut
    bool wantShortcut = settings.value(SettingsKeys::GlobalShortcut, false).toBool();
    if (wantShortcut && !m_globalShortcut && GlobalShortcut::isSupported()) {
        m_globalShortcut = new GlobalShortcut(this);
        connect(m_globalShortcut, &GlobalShortcut::activated, this, &MainWindow::showQuickSearch);
        m_globalShortcut->enable();
    } else if (!wantShortcut && m_globalShortcut) {
        delete m_globalShortcut;
        m_globalShortcut = nullptr;
    }
}

void MainWindow::handleCommand(const QString &command)
{
    if (command == "popup" || command.startsWith("popup ")) {
        showQuickSearch(command.mid(6).trimmed());
    } else if (command == "show") {
        showWindow();
    }
}

void MainWindow::showWindow()
{
    if (isMinimized()) {
        showNormal();
    } else {
        show();
    }
    raise();
    activateWindow();
}

void MainWindow::showQuickSearch(const QString &activationToken)
{
    if (!ensureSaved()) {
        showWindow();
        return;
    }
    m_quickSearch->popup(activationToken);
}

void MainWindow::openSnippet(int id)
{
    showWindow();
    if (!ensureSaved()) return;
    // Show it in the full list, so it's visible
    m_searchEdit->clear();
    if (m_tagFilterCombo->currentIndex() != 0) {
        m_tagFilterCombo->setCurrentIndex(0);
    }
    m_model->search(QString());
    selectSnippet(id);
    loadSnippet(id);
}

void MainWindow::onQuit()
{
    m_quitting = true;
    // Closing saves (or asks about) unsaved changes; it can be cancelled
    bool closed = isVisible() ? close() : ensureSaved();
    m_quitting = false;
    if (!closed) {
        showWindow();
        return;
    }
    QApplication::quit();
}

void MainWindow::setupUI()
{
    QWidget *centralWidget = new QWidget;
    setCentralWidget(centralWidget);

    m_splitter = new QSplitter(Qt::Horizontal);

    // Left panel
    QWidget *leftPanel = new QWidget;
    QVBoxLayout *leftLayout = new QVBoxLayout(leftPanel);

    m_searchEdit = new QLineEdit;
    m_searchEdit->setObjectName("searchEdit");
    m_searchEdit->setPlaceholderText("Search snippets... (Ctrl+F)");
    m_searchEdit->setClearButtonEnabled(true);
    m_searchEdit->installEventFilter(this);
    leftLayout->addWidget(m_searchEdit);

    QHBoxLayout *filterLayout = new QHBoxLayout;
    filterLayout->addWidget(new QLabel("Show:"));
    m_tagFilterCombo = new QComboBox;
    m_tagFilterCombo->setObjectName("tagFilter");
    m_tagFilterCombo->setToolTip("Show all snippets, or only those with a tag");
    m_tagFilterCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    filterLayout->addWidget(m_tagFilterCombo, 1);
    leftLayout->addLayout(filterLayout);

    QHBoxLayout *sortLayout = new QHBoxLayout;
    sortLayout->addWidget(new QLabel("Sort:"));
    m_sortCombo = new QComboBox;
    m_sortCombo->addItem("Title (A-Z)", Database::TitleAscending);
    m_sortCombo->addItem("Title (Z-A)", Database::TitleDescending);
    m_sortCombo->addItem("Oldest first", Database::CreatedAscending);
    m_sortCombo->addItem("Newest first", Database::CreatedDescending);
    sortLayout->addWidget(m_sortCombo, 1);
    leftLayout->addLayout(sortLayout);

    m_snippetList = new QListView;
    m_snippetList->setObjectName("snippetList");
    m_snippetList->setUniformItemSizes(true);
    leftLayout->addWidget(m_snippetList);

    QHBoxLayout *leftButtonLayout = new QHBoxLayout;
    m_addButton = new QPushButton("Add");
    m_editButton = new QPushButton("Edit");
    m_deleteButton = new QPushButton("Delete");
    m_deleteButton->setObjectName("deleteButton");
    m_restoreButton = new QPushButton("Restore");
    m_restoreButton->setObjectName("restoreButton");
    leftButtonLayout->addWidget(m_addButton);
    leftButtonLayout->addWidget(m_editButton);
    leftButtonLayout->addWidget(m_restoreButton);
    leftButtonLayout->addWidget(m_deleteButton);
    leftLayout->addLayout(leftButtonLayout);

    // Right panel
    QWidget *rightPanel = new QWidget;
    QVBoxLayout *rightLayout = new QVBoxLayout(rightPanel);

    m_modeLabel = new QLabel;
    m_modeLabel->setStyleSheet("font-weight: bold; color: #666;");
    rightLayout->addWidget(m_modeLabel);

    m_editor = new SnippetEditor;
    rightLayout->addWidget(m_editor, 1);

    QHBoxLayout *rightButtonLayout = new QHBoxLayout;
    m_saveButton = new QPushButton("Save");
    m_saveButton->setObjectName("saveButton");
    m_copyButton = new QPushButton("Copy");
    rightButtonLayout->addWidget(m_saveButton);
    rightButtonLayout->addWidget(m_copyButton);
    rightButtonLayout->addStretch();
    rightLayout->addLayout(rightButtonLayout);

    m_splitter->addWidget(leftPanel);
    m_splitter->addWidget(rightPanel);
    m_splitter->setChildrenCollapsible(false);
    m_splitter->setSizes({300, 600});

    QHBoxLayout *mainLayout = new QHBoxLayout(centralWidget);
    mainLayout->addWidget(m_splitter);
}

void MainWindow::setupActions()
{
    m_newAction = new QAction("&New Snippet", this);
    m_newAction->setShortcut(QKeySequence::New);
    connect(m_newAction, &QAction::triggered, this, &MainWindow::onAddSnippet);

    m_saveAction = new QAction("&Save", this);
    m_saveAction->setShortcut(QKeySequence::Save);
    connect(m_saveAction, &QAction::triggered, this, &MainWindow::onSaveSnippet);

    m_editAction = new QAction("&Edit Content", this);
    m_editAction->setShortcut(QKeySequence("Ctrl+E"));
    connect(m_editAction, &QAction::triggered, this, &MainWindow::onEditSnippet);

    m_copyAction = new QAction("&Copy Snippet", this);
    m_copyAction->setShortcut(QKeySequence("Ctrl+Shift+C"));
    connect(m_copyAction, &QAction::triggered, this, &MainWindow::onCopySnippet);

    // Delete only applies in the list, so it doesn't interfere with typing
    m_deleteAction = new QAction("&Delete", this);
    m_deleteAction->setShortcut(QKeySequence::Delete);
    m_deleteAction->setShortcutContext(Qt::WidgetShortcut);
    m_snippetList->addAction(m_deleteAction);
    connect(m_deleteAction, &QAction::triggered, this, &MainWindow::onDeleteSnippet);

    m_restoreAction = new QAction("&Restore", this);
    m_restoreAction->setToolTip("Move the snippet out of the trash");
    connect(m_restoreAction, &QAction::triggered, this, &MainWindow::onRestoreSnippet);

    // Only fires when the focus isn't in a text field, which has its own undo
    m_undoDeleteAction = new QAction("&Undo Delete", this);
    m_undoDeleteAction->setShortcut(QKeySequence::Undo);
    connect(m_undoDeleteAction, &QAction::triggered, this, &MainWindow::onUndoDelete);

    m_emptyTrashAction = new QAction("Empty &Trash...", this);
    connect(m_emptyTrashAction, &QAction::triggered, this, &MainWindow::onEmptyTrash);

    m_detailsAction = new QAction("Snippet &Details...", this);
    m_detailsAction->setShortcut(QKeySequence("Alt+Return"));
    connect(m_detailsAction, &QAction::triggered, this, &MainWindow::onShowDetails);

    // Right-click menu for the snippet list
    m_snippetList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_snippetList, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        if (!m_snippetList->indexAt(pos).isValid() || currentId() == -1) return;
        QMenu menu(this);
        menu.addAction(m_copyAction);
        if (inTrashView()) {
            menu.addAction(m_restoreAction);
            menu.addAction(m_detailsAction);
            menu.addSeparator();
            menu.addAction(m_deleteAction);
            menu.addAction(m_emptyTrashAction);
        } else {
            menu.addAction(m_editAction);
            menu.addAction(m_detailsAction);
            menu.addSeparator();
            menu.addAction(m_deleteAction);
        }
        menu.exec(m_snippetList->viewport()->mapToGlobal(pos));
    });

    m_findAction = new QAction("&Find", this);
    m_findAction->setShortcut(QKeySequence::Find);
    connect(m_findAction, &QAction::triggered, this, [this]() {
        m_searchEdit->setFocus();
        m_searchEdit->selectAll();
    });
}

void MainWindow::setupMenus()
{
    QMenu *fileMenu = menuBar()->addMenu("&File");

    QAction *importAction = fileMenu->addAction("&Import...");
    importAction->setToolTip("Add snippets from a JSON, Markdown or CSV file");
    connect(importAction, &QAction::triggered, this, &MainWindow::onImport);
    QAction *exportAction = fileMenu->addAction("&Export...");
    exportAction->setToolTip("Save snippets to a JSON, Markdown or CSV file");
    connect(exportAction, &QAction::triggered, this, &MainWindow::onExport);
    fileMenu->addSeparator();

    QAction *settingsAction = fileMenu->addAction("&Settings...");
    settingsAction->setShortcut(QKeySequence::Preferences);
    connect(settingsAction, &QAction::triggered, this, &MainWindow::onShowSettings);

    fileMenu->addSeparator();

    QAction *quitAction = fileMenu->addAction("&Quit");
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, this, &MainWindow::onQuit);

    QMenu *editMenu = menuBar()->addMenu("&Edit");
    editMenu->addAction(m_undoDeleteAction);
    editMenu->addSeparator();
    editMenu->addAction(m_newAction);
    editMenu->addAction(m_editAction);
    editMenu->addAction(m_saveAction);
    editMenu->addSeparator();
    editMenu->addAction(m_copyAction);
    editMenu->addAction(m_detailsAction);
    editMenu->addAction(m_deleteAction);
    editMenu->addAction(m_restoreAction);
    editMenu->addAction(m_emptyTrashAction);
    editMenu->addSeparator();
    editMenu->addAction(m_findAction);

    QMenu *helpMenu = menuBar()->addMenu("&Help");
    QAction *aboutAction = helpMenu->addAction("&About Snippet Manager");
    connect(aboutAction, &QAction::triggered, this, &MainWindow::onAbout);
}

int MainWindow::currentId() const
{
    return m_editor->snippet().id;
}

void MainWindow::onShowDetails()
{
    if (currentId() == -1) return;

    // Use the stored snippet, so the details match what's saved
    Snippet snippet = Database::instance().snippet(currentId());
    QLocale locale;
    auto formatTime = [&locale](const QDateTime &time) {
        return time.isValid() ? locale.toString(time, QLocale::LongFormat) : QString("Unknown");
    };

    QString type = snippet.isRich() ? "Rich text"
                   : snippet.language.isEmpty() ? "Plain text" : snippet.language;
    QString plain = snippet.content;
    QString details = QString("Title: %1\n\nCreated: %2\nModified: %3\n\nType: %4\nTags: %5\nSize: %6 characters, %7 lines")
                          .arg(snippet.title, formatTime(snippet.created), formatTime(snippet.modified), type,
                               snippet.tags.isEmpty() ? QString("None") : snippet.tags.join(", "))
                          .arg(plain.size())
                          .arg(plain.isEmpty() ? 0 : plain.count('\n') + 1);
    if (snippet.inTrash()) {
        details += "\n\nMoved to the trash: " + formatTime(snippet.deleted);
    }
    MessageBox::information(this, "Snippet Details", details);
}

static const char *ImportFilter = "Snippet files (*.json *.md *.markdown *.csv *.tsv *.txt);;"
                                  "JSON (*.json);;Markdown (*.md *.markdown);;CSV (*.csv *.tsv *.txt)";
static const char *ExportJsonFilter = "JSON - everything, best for backups (*.json)";
static const char *ExportMarkdownFilter = "Markdown - easy to read (*.md)";
static const char *ExportCsvFilter = "CSV - for spreadsheets (*.csv)";
static const char *LastFolderKey = "importExport/lastFolder";

void MainWindow::onImport()
{
    if (!ensureSaved()) return;
    QSettings settings;
    QString startFolder = settings.value(LastFolderKey, QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).toString();
    QString path = QFileDialog::getOpenFileName(this, "Import Snippets", startFolder, ImportFilter);
    if (path.isEmpty()) return;
    settings.setValue(LastFolderKey, QFileInfo(path).absolutePath());

    QString error;
    QList<Snippet> snippets = ImportExport::readFile(path, &error);
    if (snippets.isEmpty()) {
        MessageBox::warning(this, "Import Snippets",
                            QString("No snippets could be imported from '%1'.\n\n%2")
                                .arg(QDir::toNativeSeparators(path), error));
        return;
    }
    if (!MessageBox::question(this, "Import Snippets",
                              QString("Import %1 snippet(s) from '%2'?\n\nSnippets with the same title and content "
                                      "as one you already have are skipped.")
                                  .arg(snippets.size()).arg(QFileInfo(path).fileName()),
                              QMessageBox::Yes)) {
        return;
    }

    int added = 0, skipped = 0;
    if (!Database::instance().importSnippets(snippets, &added, &skipped, &error)) {
        MessageBox::warning(this, "Import Snippets", "The snippets could not be imported. Nothing was changed.\n\n" + error);
        return;
    }
    m_model->refresh();
    refreshTags();
    selectSnippet(currentId());
    QString message = QString("Imported %1 snippet(s)").arg(added);
    if (skipped > 0) {
        message += QString(", skipped %1 already here").arg(skipped);
    }
    statusBar()->showMessage(message, 8000);
}

void MainWindow::onExport()
{
    if (!ensureSaved()) return;

    // Export what's listed, or everything, if the list is filtered
    QList<Snippet> all = Database::instance().snippets();
    QList<Snippet> shown;
    for (int row = 0; row < m_model->rowCount(); ++row) {
        shown.append(m_model->getSnippet(row));
    }
    QList<Snippet> snippets = all;
    bool filtered = !inTrashView() && shown.size() != all.size();
    if (filtered) {
        QMessageBox box(QMessageBox::Question, "Export Snippets",
                        "The list is filtered. Which snippets do you want to export?",
                        QMessageBox::Cancel, this);
        QPushButton *shownButton = box.addButton(QString("Those Shown (%1)").arg(shown.size()), QMessageBox::AcceptRole);
        box.addButton(QString("All (%1)").arg(all.size()), QMessageBox::AcceptRole);
        box.exec();
        if (box.clickedButton() == box.button(QMessageBox::Cancel)) return;
        if (box.clickedButton() == shownButton) snippets = shown;
    }
    if (snippets.isEmpty()) {
        MessageBox::information(this, "Export Snippets", "There are no snippets to export.");
        return;
    }

    QSettings settings;
    QString folder = settings.value(LastFolderKey, QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).toString();
    QString selectedFilter = ExportJsonFilter;
    QString path = QFileDialog::getSaveFileName(this, "Export Snippets", folder + "/snippets.json",
                                                QString("%1;;%2;;%3").arg(ExportJsonFilter, ExportMarkdownFilter, ExportCsvFilter),
                                                &selectedFilter);
    if (path.isEmpty()) return;
    settings.setValue(LastFolderKey, QFileInfo(path).absolutePath());

    // Use the chosen type's extension if the name doesn't have a supported one
    if (ImportExport::formatForPath(path) == ImportExport::Format::Unknown) {
        path += selectedFilter == ExportMarkdownFilter ? ".md" : selectedFilter == ExportCsvFilter ? ".csv" : ".json";
    }

    QString error;
    if (!ImportExport::writeFile(path, snippets, &error)) {
        MessageBox::warning(this, "Export Snippets", "The snippets could not be exported.\n\n" + error);
        return;
    }
    statusBar()->showMessage(QString("Exported %1 snippet(s) to %2").arg(snippets.size()).arg(QDir::toNativeSeparators(path)), 8000);
}

void MainWindow::onAbout()
{
    QMessageBox::about(this, "About Snippet Manager",
                       QString("<h3>Snippet Manager %1</h3>"
                               "<p>A simple manager for text and code snippets.</p>"
                               "<p>Built with Qt %2</p>"
                               "<p>Database: %3</p>")
                           .arg(QApplication::applicationVersion(),
                                QT_VERSION_STR,
                                QDir::toNativeSeparators(Database::instance().databasePath()).toHtmlEscaped()));
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    // In the search box, Escape clears the search and Down moves into the list
    if (watched == m_searchEdit && event->type() == QEvent::KeyPress) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->key() == Qt::Key_Escape && !m_searchEdit->text().isEmpty()) {
            m_searchEdit->clear();
            return true;
        }
        if (keyEvent->key() == Qt::Key_Down && m_model->rowCount() > 0) {
            m_snippetList->setFocus();
            if (!m_snippetList->currentIndex().isValid()) {
                m_snippetList->setCurrentIndex(m_model->index(0, 0));
            }
            return true;
        }
    }

    // While viewing (not editing) a snippet, a click anywhere in the content
    // copies it. If the user dragged to select part of the text, only the
    // selection is copied. Clicks while editing are left alone so the
    // clipboard isn't overwritten before the user can paste into the snippet.
    // This can be turned off in the settings.
    if (m_editor && watched == m_editor->contentViewport() && event->type() == QEvent::MouseButtonRelease) {
        QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() == Qt::LeftButton && currentId() != -1 && !m_editor->isContentEditable()
            && copyOnSelect()) {
            QTextDocumentFragment selected = m_editor->selection();
            if (!selected.isEmpty()) {
                QString plain = selected.toPlainText();
                Clipboard::copy(plain, m_editor->showsFormatting() ? selected.toHtml() : QString());
                statusBar()->showMessage("Selected text copied to clipboard", 2000);
            } else if (!m_editor->plainText().isEmpty()) {
                Clipboard::copySnippet(m_editor->snippet());
                statusBar()->showMessage("Content copied to clipboard", 2000);
            }
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (!ensureSaved()) {
        if (!MessageBox::question(this, "Quit", "Quit anyway and lose the unsaved changes?")) {
            event->ignore();
            return;
        }
        m_editor->setModified(false);
    }

    QSettings settings;
    settings.setValue(GeometryKey, saveGeometry());
    settings.setValue(SplitterKey, m_splitter->saveState());

    // With a tray icon, closing the window just hides it
    if (!m_quitting && canRunInBackground()) {
        hide();
        event->ignore();
        if (!settings.value("quickAccess/trayHintShown", false).toBool()) {
            settings.setValue("quickAccess/trayHintShown", true);
            m_tray->showMessage("Snippet Manager is still running",
                                "It's in the system tray, ready for quick search. Use File > Quit to close it completely.",
                                QSystemTrayIcon::Information, 6000);
        }
        return;
    }
    event->accept();
}

bool MainWindow::copyOnSelect() const
{
    return QSettings().value(SettingsKeys::CopyOnSelect, true).toBool();
}

void MainWindow::setContentEditable(bool editable)
{
    m_editor->setContentEditable(editable);
    updateModeLabel();
}

void MainWindow::updateModeLabel()
{
    if (currentId() == -1) {
        m_modeLabel->setText("Select a snippet to view");
    } else if (inTrashView()) {
        m_modeLabel->setText("In the trash - Restore it to make changes");
    } else if (m_editor->isContentEditable()) {
        m_modeLabel->setText("Editing snippet - Make changes and click 'Save'");
    } else if (copyOnSelect()) {
        m_modeLabel->setText("Viewing snippet - Click the content to copy it");
    } else {
        m_modeLabel->setText("Viewing snippet - Click 'Copy' to copy it");
    }
}

void MainWindow::loadSnippet(int id)
{
    m_editor->setSnippet(id != -1 ? Database::instance().snippet(id) : Snippet());
    m_editor->setLocked(inTrashView());
    updateModeLabel();
    updateButtonStates();
}

void MainWindow::selectSnippet(int id)
{
    m_updatingSelection = true;
    int row = m_model->rowForId(id);
    if (row >= 0) {
        QModelIndex index = m_model->index(row, 0);
        m_snippetList->setCurrentIndex(index);
        m_snippetList->scrollTo(index);
    } else {
        m_snippetList->setCurrentIndex(QModelIndex());
    }
    m_updatingSelection = false;
}

void MainWindow::refreshTags()
{
    QList<QPair<QString, int>> counts = Database::instance().tagCounts();

    QStringList names;
    for (const auto &count : counts) {
        names << count.first;
    }
    m_editor->setKnownTags(names);

    // Rebuild the filter, keeping the current choice if it still exists
    int kind = m_tagFilterCombo->currentData(TagFilterKindRole).toInt();
    QString tag = m_tagFilterCombo->currentData(TagFilterNameRole).toString();

    const QSignalBlocker blocker(m_tagFilterCombo);
    m_tagFilterCombo->clear();
    m_tagFilterCombo->addItem("All snippets", AllSnippets);
    m_tagFilterCombo->addItem("Untagged", Untagged);
    if (!counts.isEmpty()) {
        m_tagFilterCombo->insertSeparator(m_tagFilterCombo->count());
    }
    for (const auto &count : counts) {
        m_tagFilterCombo->addItem(QString("%1 (%2)").arg(count.first).arg(count.second), OneTag);
        m_tagFilterCombo->setItemData(m_tagFilterCombo->count() - 1, count.first, TagFilterNameRole);
    }
    m_tagFilterCombo->insertSeparator(m_tagFilterCombo->count());
    int trashed = Database::instance().trashCount();
    m_tagFilterCombo->addItem(QIcon::fromTheme("user-trash"), trashed ? QString("Trash (%1)").arg(trashed) : QString("Trash"));
    m_tagFilterCombo->setItemData(m_tagFilterCombo->count() - 1, Trash, TagFilterKindRole);

    int index = 0;
    for (int i = 0; i < m_tagFilterCombo->count(); ++i) {
        if (m_tagFilterCombo->itemData(i, TagFilterKindRole).toInt() == kind
            && (kind != OneTag || m_tagFilterCombo->itemData(i, TagFilterNameRole).toString() == tag)) {
            index = i;
            break;
        }
    }
    m_tagFilterCombo->setCurrentIndex(index);

    // If the chosen tag has gone, the list now shows all snippets
    if (index == 0 && kind != AllSnippets) {
        m_model->setTagFilter(QString(), false);
    }
}

bool MainWindow::inTrashView() const
{
    return m_model && m_model->showsTrash();
}

void MainWindow::onTagFilterChanged()
{
    saveCurrentSnippet();
    int kind = m_tagFilterCombo->currentData(TagFilterKindRole).toInt();
    bool trash = kind == Trash;
    bool wasTrash = inTrashView();
    m_model->setTagFilter(kind == OneTag ? m_tagFilterCombo->currentData(TagFilterNameRole).toString() : QString(),
                          kind == Untagged);
    m_model->setShowTrash(trash);
    if (trash != wasTrash) {
        // The snippet shown belongs to the other view
        selectSnippet(-1);
        loadSnippet(-1);
    } else {
        selectSnippet(currentId());
    }
    m_deleteAction->setText(trash ? "&Delete Forever..." : "&Delete");
    m_deleteButton->setText(trash ? "Delete Forever" : "Delete");
    updateButtonStates();
}

void MainWindow::onSelectionChanged(const QModelIndex &current)
{
    if (m_updatingSelection) return;

    int newId = current.isValid() ? m_model->data(current, SnippetModel::IdRole).toInt() : -1;

    if (!ensureSaved()) {
        // Stay on the snippet with unsaved changes rather than throwing them away
        selectSnippet(currentId());
        return;
    }

    // Saving refreshes the model, which would lose the new selection, so restore it afterwards
    selectSnippet(newId);
    loadSnippet(newId);

    if (currentId() != -1) {
        if (copyOnSelect()) {
            Clipboard::copySnippet(m_editor->snippet());
            statusBar()->showMessage("Content copied to clipboard - Rename using the title field, click 'Edit' to change the content", 4000);
        } else {
            statusBar()->showMessage("Rename using the title field, click 'Edit' to change the content", 4000);
        }
    } else {
        statusBar()->showMessage("Select a snippet to view, or click 'Add' to create a new one");
    }
}

void MainWindow::onSearchTextChanged()
{
    saveCurrentSnippet();
    m_model->search(m_searchEdit->text());
    selectSnippet(currentId());

    // Search results are ranked by how well they match, rather than sorted
    bool ranked = !m_searchEdit->text().trimmed().isEmpty() && Database::instance().hasSearchIndex();
    m_sortCombo->setEnabled(!ranked);
    m_sortCombo->setToolTip(ranked ? "Search results are sorted by best match" : QString());
}

void MainWindow::onSortOrderChanged()
{
    auto order = static_cast<Database::SortOrder>(m_sortCombo->currentData().toInt());
    QSettings().setValue(SortOrderKey, static_cast<int>(order));

    saveCurrentSnippet();
    m_model->setSortOrder(order);
    selectSnippet(currentId());
}

void MainWindow::onAddSnippet()
{
    if (!ensureSaved()) return;

    // New snippets go in the normal list, not the trash
    if (inTrashView()) {
        m_tagFilterCombo->setCurrentIndex(0);
    }

    // Clear any search so the new snippet is visible in the list
    if (!m_searchEdit->text().isEmpty()) {
        m_searchTimer->stop();
        const QSignalBlocker blocker(m_searchEdit);
        m_searchEdit->clear();
        m_model->search(QString());
    }

    // New snippets get the tag being shown, so they stay in the list
    Snippet snippet;
    snippet.title = "New Snippet";
    if (m_tagFilterCombo->currentData(TagFilterKindRole).toInt() == OneTag) {
        snippet.tags = {m_tagFilterCombo->currentData(TagFilterNameRole).toString()};
    }

    QString error;
    int id = Database::instance().addSnippet(snippet, &error);
    if (id == -1) {
        MessageBox::warning(this, "Add Snippet", "The snippet could not be added.\n\n" + error);
        return;
    }

    m_model->refresh();
    refreshTags();
    selectSnippet(id);
    loadSnippet(id);
    setContentEditable(true);
    updateButtonStates();
    m_editor->focusTitle();
}

void MainWindow::onEditSnippet()
{
    setContentEditable(true);
    m_editor->focusContent();
    statusBar()->showMessage("Editing snippet - Make changes and click 'Save'");
    updateButtonStates();
}

void MainWindow::onSaveSnippet()
{
    if (ensureSaved()) {
        setContentEditable(false);
    }
    updateButtonStates();
}

void MainWindow::onDeleteSnippet()
{
    int id = currentId();
    if (id == -1) return;
    QString title = m_editor->snippet().title;
    QString error;

    if (inTrashView()) {
        if (!MessageBox::question(this, "Delete Forever",
                                  QString("Permanently delete '%1'? This can't be undone.").arg(title))) {
            return;
        }
        if (!Database::instance().deleteSnippet(id, &error)) {
            MessageBox::warning(this, "Delete Forever", "The snippet could not be deleted.\n\n" + error);
            return;
        }
        m_recentlyTrashed.removeAll(id);
        statusBar()->showMessage(QString("'%1' was deleted").arg(title), 4000);
    } else {
        // Keep any pending edits with the snippet in the trash
        if (!ensureSaved()) return;
        if (!Database::instance().moveToTrash(id, &error)) {
            MessageBox::warning(this, "Delete Snippet", "The snippet could not be moved to the trash.\n\n" + error);
            return;
        }
        m_recentlyTrashed.append(id);
        statusBar()->showMessage(QString("Moved '%1' to the trash - Press %2 to undo")
                                     .arg(title, m_undoDeleteAction->shortcut().toString(QKeySequence::NativeText)),
                                 8000);
    }

    m_editor->setModified(false);
    int row = m_model->rowForId(id);
    m_model->refresh();
    refreshTags();
    // Select the next snippet, so several can be deleted in a row
    int next = qMin(row, m_model->rowCount() - 1);
    int nextId = next >= 0 ? m_model->data(m_model->index(next, 0), SnippetModel::IdRole).toInt() : -1;
    selectSnippet(nextId);
    loadSnippet(nextId);
}

void MainWindow::onUndoDelete()
{
    // The restored snippet replaces the one being shown
    if (!ensureSaved()) return;
    while (!m_recentlyTrashed.isEmpty()) {
        int id = m_recentlyTrashed.takeLast();
        if (Database::instance().restoreFromTrash(id)) {
            m_model->refresh();
            refreshTags();
            if (!inTrashView()) {
                selectSnippet(id);
                loadSnippet(id);
            } else {
                selectSnippet(-1);
                loadSnippet(-1);
            }
            statusBar()->showMessage(QString("Restored '%1'").arg(Database::instance().snippet(id).title), 4000);
            return;
        }
        // Already restored or deleted for good; try the one before
    }
    statusBar()->showMessage("Nothing to undo", 2000);
}

void MainWindow::onRestoreSnippet()
{
    int id = currentId();
    if (id == -1 || !inTrashView()) return;
    QString error;
    if (!Database::instance().restoreFromTrash(id, &error)) {
        MessageBox::warning(this, "Restore", "The snippet could not be restored.\n\n" + error);
        return;
    }
    m_recentlyTrashed.removeAll(id);
    QString title = m_editor->snippet().title;
    m_model->refresh();
    refreshTags();
    selectSnippet(-1);
    loadSnippet(-1);
    statusBar()->showMessage(QString("Restored '%1'").arg(title), 4000);
}

void MainWindow::onEmptyTrash()
{
    int count = Database::instance().trashCount();
    if (count == 0) return;
    if (!MessageBox::question(this, "Empty Trash",
                              QString("Permanently delete the %1 snippet(s) in the trash? This can't be undone.").arg(count))) {
        return;
    }
    QString error;
    if (!Database::instance().emptyTrash(&error)) {
        MessageBox::warning(this, "Empty Trash", "The trash could not be emptied.\n\n" + error);
        return;
    }
    m_recentlyTrashed.clear();
    m_model->refresh();
    refreshTags();
    if (inTrashView()) {
        selectSnippet(-1);
        loadSnippet(-1);
    }
    statusBar()->showMessage("The trash was emptied", 4000);
}

void MainWindow::onCopySnippet()
{
    Clipboard::copySnippet(m_editor->snippet());
    statusBar()->showMessage("Content copied to clipboard", 2000);
}

void MainWindow::onEditorChanged()
{
    updateButtonStates();
}

void MainWindow::onShowSettings()
{
    SettingsDialog dialog(this);
    dialog.setSavePendingChanges([this]() { return ensureSaved(); });
    connect(&dialog, &SettingsDialog::clipboardSettingsChanged, this, &MainWindow::updateModeLabel);
    connect(&dialog, &SettingsDialog::editorSettingsChanged, m_editor, &SnippetEditor::applySettings);
    connect(&dialog, &SettingsDialog::quickAccessSettingsChanged, this, [this, &dialog]() {
        applyQuickAccessSettings();
        dialog.setGlobalShortcut(m_globalShortcut);
    });
    dialog.setGlobalShortcut(m_globalShortcut);
    connect(&dialog, &SettingsDialog::databaseChanged, this, &MainWindow::onDatabaseChanged);
    dialog.exec();
}

void MainWindow::onDatabaseChanged()
{
    m_editor->setModified(false);
    m_recentlyTrashed.clear();
    m_model->refresh();
    refreshTags();
    selectSnippet(-1);
    loadSnippet(-1);
    statusBar()->showMessage("Using database " + QDir::toNativeSeparators(Database::instance().databasePath()), 5000);
}

void MainWindow::updateButtonStates()
{
    bool hasSelection = currentId() != -1;
    bool hasContent = !m_editor->plainText().isEmpty();

    bool trash = inTrashView();
    m_editAction->setEnabled(hasSelection && !m_editor->isContentEditable() && !trash);
    m_restoreAction->setEnabled(hasSelection && trash);
    m_restoreButton->setVisible(trash);
    m_restoreButton->setEnabled(m_restoreAction->isEnabled());
    m_emptyTrashAction->setEnabled(Database::instance().trashCount() > 0);
    m_editButton->setVisible(!trash);
    m_deleteAction->setEnabled(hasSelection);
    m_detailsAction->setEnabled(hasSelection);
    m_saveAction->setEnabled(m_editor->isModified() && hasSelection);
    m_copyAction->setEnabled(hasContent);
    m_editButton->setEnabled(m_editAction->isEnabled());
    m_deleteButton->setEnabled(m_deleteAction->isEnabled());
    m_saveButton->setEnabled(m_saveAction->isEnabled());
    m_copyButton->setEnabled(m_copyAction->isEnabled());
}

bool MainWindow::saveCurrentSnippet(QString *errorMessage)
{
    if (!m_editor->isModified() || currentId() == -1) {
        return true;
    }

    Snippet snippet = m_editor->snippet();
    if (!Database::instance().updateSnippet(snippet, errorMessage)) {
        statusBar()->showMessage("The snippet could not be saved");
        return false;
    }

    // Show exactly what was saved (e.g. tidied tags)
    m_editor->setSaved(Database::instance().snippet(snippet.id));
    m_model->refresh();
    refreshTags();
    selectSnippet(snippet.id);
    updateButtonStates();
    statusBar()->showMessage("Snippet saved successfully", 2000);
    return true;
}

bool MainWindow::ensureSaved()
{
    QString error;
    if (saveCurrentSnippet(&error)) {
        return true;
    }

    MessageBox::warning(this, "Save Snippet",
                        QString("Your changes to '%1' could not be saved.\n\n%2\n\n"
                                "They are still in the editor. Check that the database location in "
                                "File > Settings... is available and writable, then try saving again.")
                            .arg(m_editor->snippet().title, error));
    return false;
}
