#include "mainwindow.h"
#include "clipboard.h"
#include "database.h"
#include "messagebox.h"
#include "settingsdialog.h"
#include "snippeteditor.h"
#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
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
#include <QSettings>
#include <QSplitter>
#include <QStatusBar>
#include <QTimer>
#include <QVBoxLayout>

static const char *SortOrderKey = "view/sortOrder";
static const char *GeometryKey = "window/geometry";
static const char *SplitterKey = "window/splitter";

// What each entry in the tag filter shows
enum TagFilterKind { AllSnippets, Untagged, OneTag };
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
}

MainWindow::~MainWindow()
{
    saveCurrentSnippet();
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
    leftButtonLayout->addWidget(m_addButton);
    leftButtonLayout->addWidget(m_editButton);
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

    m_detailsAction = new QAction("Snippet &Details...", this);
    m_detailsAction->setShortcut(QKeySequence("Alt+Return"));
    connect(m_detailsAction, &QAction::triggered, this, &MainWindow::onShowDetails);

    // Right-click menu for the snippet list
    m_snippetList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_snippetList, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        if (!m_snippetList->indexAt(pos).isValid() || currentId() == -1) return;
        QMenu menu(this);
        menu.addAction(m_copyAction);
        menu.addAction(m_editAction);
        menu.addAction(m_detailsAction);
        menu.addSeparator();
        menu.addAction(m_deleteAction);
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

    QAction *settingsAction = fileMenu->addAction("&Settings...");
    settingsAction->setShortcut(QKeySequence::Preferences);
    connect(settingsAction, &QAction::triggered, this, &MainWindow::onShowSettings);

    fileMenu->addSeparator();

    QAction *quitAction = fileMenu->addAction("&Quit");
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, this, &QMainWindow::close);

    QMenu *editMenu = menuBar()->addMenu("&Edit");
    editMenu->addAction(m_newAction);
    editMenu->addAction(m_editAction);
    editMenu->addAction(m_saveAction);
    editMenu->addSeparator();
    editMenu->addAction(m_copyAction);
    editMenu->addAction(m_detailsAction);
    editMenu->addAction(m_deleteAction);
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

void MainWindow::onTagFilterChanged()
{
    saveCurrentSnippet();
    int kind = m_tagFilterCombo->currentData(TagFilterKindRole).toInt();
    m_model->setTagFilter(kind == OneTag ? m_tagFilterCombo->currentData(TagFilterNameRole).toString() : QString(),
                          kind == Untagged);
    selectSnippet(currentId());
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
    if (currentId() == -1) return;

    QString title = m_editor->snippet().title;
    if (!MessageBox::question(this, "Delete Snippet",
                              QString("Are you sure you want to delete '%1'?").arg(title))) {
        return;
    }

    QString error;
    if (!Database::instance().deleteSnippet(currentId(), &error)) {
        MessageBox::warning(this, "Delete Snippet", "The snippet could not be deleted.\n\n" + error);
        return;
    }
    m_editor->setModified(false);
    m_model->refresh();
    refreshTags();
    selectSnippet(-1);
    loadSnippet(-1);
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
    connect(&dialog, &SettingsDialog::databaseChanged, this, &MainWindow::onDatabaseChanged);
    dialog.exec();
}

void MainWindow::onDatabaseChanged()
{
    m_editor->setModified(false);
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

    m_editAction->setEnabled(hasSelection && !m_editor->isContentEditable());
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
