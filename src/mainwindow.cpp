#include "mainwindow.h"
#include "database.h"
#include "settingsdialog.h"
#include "messagebox.h"
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QMimeData>
#include <QMessageBox>
#include <QHeaderView>
#include <QStatusBar>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QLocale>
#include <QSettings>
#include <QIcon>
#include <QDebug>
#include <QDir>
#include <QCoreApplication>
#include <QFile>

static const char *SortOrderKey = "view/sortOrder";
static const char *GeometryKey = "window/geometry";
static const char *SplitterKey = "window/splitter";

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_currentSnippetId(-1)
    , m_isModified(false)
    , m_updatingSelection(false)
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
    
    m_searchTimer = new QTimer(this);
    m_searchTimer->setSingleShot(true);
    m_searchTimer->setInterval(300);
    connect(m_searchTimer, &QTimer::timeout, this, &MainWindow::onSearchTextChanged);
    
    // Connect signals
    connect(m_snippetList->selectionModel(), &QItemSelectionModel::currentChanged,
            this, &MainWindow::onSelectionChanged);
    connect(m_searchEdit, &QLineEdit::textChanged, this, [this]() { m_searchTimer->start(); });
    connect(m_sortCombo, &QComboBox::currentIndexChanged, this, &MainWindow::onSortOrderChanged);
    connect(m_addButton, &QPushButton::clicked, m_newAction, &QAction::trigger);
    connect(m_editButton, &QPushButton::clicked, m_editAction, &QAction::trigger);
    connect(m_deleteButton, &QPushButton::clicked, m_deleteAction, &QAction::trigger);
    connect(m_saveButton, &QPushButton::clicked, m_saveAction, &QAction::trigger);
    connect(m_copyButton, &QPushButton::clicked, m_copyAction, &QAction::trigger);
    connect(m_titleEdit, &QLineEdit::textChanged, this, &MainWindow::onTitleChanged);
    connect(m_titleEdit, &QLineEdit::returnPressed, this, &MainWindow::ensureSaved);
    connect(m_contentEdit, &QTextEdit::textChanged, this, &MainWindow::onContentChanged);

    // Clicking in the content area copies it to the clipboard
    m_contentEdit->viewport()->installEventFilter(this);
    
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
    m_centralWidget = new QWidget;
    setCentralWidget(m_centralWidget);
    
    m_splitter = new QSplitter(Qt::Horizontal);
    
    // Left panel
    m_leftPanel = new QWidget;
    QVBoxLayout *leftLayout = new QVBoxLayout(m_leftPanel);
    
    m_searchEdit = new QLineEdit;
    m_searchEdit->setPlaceholderText("Search snippets... (Ctrl+F)");
    m_searchEdit->setClearButtonEnabled(true);
    m_searchEdit->installEventFilter(this);
    leftLayout->addWidget(m_searchEdit);

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
    m_snippetList->setUniformItemSizes(true);
    leftLayout->addWidget(m_snippetList);
    
    QHBoxLayout *leftButtonLayout = new QHBoxLayout;
    m_addButton = new QPushButton("Add");
    m_editButton = new QPushButton("Edit");
    m_deleteButton = new QPushButton("Delete");
    leftButtonLayout->addWidget(m_addButton);
    leftButtonLayout->addWidget(m_editButton);
    leftButtonLayout->addWidget(m_deleteButton);
    leftLayout->addLayout(leftButtonLayout);
    
    // Right panel
    m_rightPanel = new QWidget;
    QVBoxLayout *rightLayout = new QVBoxLayout(m_rightPanel);
    
    m_modeLabel = new QLabel("Select a snippet to view");
    m_modeLabel->setStyleSheet("font-weight: bold; color: #666;");
    rightLayout->addWidget(m_modeLabel);
    
    rightLayout->addWidget(new QLabel("Title:"));
    m_titleEdit = new QLineEdit;
    rightLayout->addWidget(m_titleEdit);
    
    rightLayout->addWidget(new QLabel("Content:"));
    m_contentEdit = new QTextEdit;
    m_contentEdit->setAcceptRichText(false);
    rightLayout->addWidget(m_contentEdit);
    
    QHBoxLayout *rightButtonLayout = new QHBoxLayout;
    m_saveButton = new QPushButton("Save");
    m_copyButton = new QPushButton("Copy");
    rightButtonLayout->addWidget(m_saveButton);
    rightButtonLayout->addWidget(m_copyButton);
    rightButtonLayout->addStretch();
    rightLayout->addLayout(rightButtonLayout);
    
    m_splitter->addWidget(m_leftPanel);
    m_splitter->addWidget(m_rightPanel);
    m_splitter->setChildrenCollapsible(false);
    m_splitter->setSizes({300, 600});
    
    QHBoxLayout *mainLayout = new QHBoxLayout(m_centralWidget);
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
        if (!m_snippetList->indexAt(pos).isValid() || m_currentSnippetId == -1) return;
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

void MainWindow::onShowDetails()
{
    if (m_currentSnippetId == -1) return;

    // Use the stored snippet, so the details match what's saved
    Snippet snippet = Database::instance().snippet(m_currentSnippetId);
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
    if (watched == m_contentEdit->viewport() && event->type() == QEvent::MouseButtonRelease) {
        QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() == Qt::LeftButton && m_currentSnippetId != -1 && !isContentEditable()
            && copyOnSelect()) {
            QString selected = m_contentEdit->textCursor().selectedText();
            if (!selected.isEmpty()) {
                // QTextCursor uses the Unicode paragraph separator for line breaks
                selected.replace(QChar::ParagraphSeparator, '\n');
                copyToClipboard(selected);
                statusBar()->showMessage("Selected text copied to clipboard", 2000);
            } else if (!m_contentEdit->toPlainText().isEmpty()) {
                copyToClipboard(m_contentEdit->toPlainText());
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
        m_isModified = false;
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

void MainWindow::copyToClipboard(const QString &text)
{
    auto *mimeData = new QMimeData;
    mimeData->setText(text);
    if (QSettings().value(SettingsKeys::HideFromHistory, false).toBool()) {
        // Tells clipboard managers that follow this convention (such as KDE's
        // Klipper) not to store the text in their history
        mimeData->setData("x-kde-passwordManagerHint", "secret");
    }
    QApplication::clipboard()->setMimeData(mimeData);
}

void MainWindow::setContentEditable(bool editable)
{
    m_contentEdit->setReadOnly(!editable);
    // Keep the text cursor visible and selectable in view mode too
    m_contentEdit->setTextInteractionFlags(editable ? Qt::TextEditorInteraction
                                                    : Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    m_contentEdit->viewport()->setCursor(editable ? Qt::IBeamCursor : Qt::PointingHandCursor);

    if (m_currentSnippetId == -1) {
        m_modeLabel->setText("Select a snippet to view");
    } else if (editable) {
        m_modeLabel->setText("Editing snippet - Make changes and click 'Save'");
    } else if (copyOnSelect()) {
        m_modeLabel->setText("Viewing snippet - Click the content to copy it");
    } else {
        m_modeLabel->setText("Viewing snippet - Click 'Copy' to copy it");
    }
}

bool MainWindow::isContentEditable() const
{
    return !m_contentEdit->isReadOnly();
}

void MainWindow::loadSnippet(int id)
{
    Snippet snippet = id != -1 ? Database::instance().snippet(id) : Snippet();
    m_current = snippet;
    m_currentSnippetId = snippet.id;

    m_titleEdit->setText(snippet.title);
    m_contentEdit->setPlainText(snippet.content);
    m_isModified = false;

    bool hasSnippet = m_currentSnippetId != -1;
    m_titleEdit->setEnabled(hasSnippet);
    m_contentEdit->setEnabled(hasSnippet);
    setContentEditable(false);
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

void MainWindow::onSelectionChanged(const QModelIndex &current)
{
    if (m_updatingSelection) return;

    int newId = current.isValid() ? m_model->data(current, SnippetModel::IdRole).toInt() : -1;

    if (!ensureSaved()) {
        // Stay on the snippet with unsaved changes rather than throwing them away
        selectSnippet(m_currentSnippetId);
        return;
    }

    // Saving refreshes the model, which would lose the new selection, so restore it afterwards
    selectSnippet(newId);
    loadSnippet(newId);

    if (m_currentSnippetId != -1) {
        if (copyOnSelect()) {
            copyToClipboard(m_contentEdit->toPlainText());
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
    selectSnippet(m_currentSnippetId);
}

void MainWindow::onSortOrderChanged()
{
    auto order = static_cast<Database::SortOrder>(m_sortCombo->currentData().toInt());
    QSettings().setValue(SortOrderKey, static_cast<int>(order));

    saveCurrentSnippet();
    m_model->setSortOrder(order);
    selectSnippet(m_currentSnippetId);
}

void MainWindow::onAddSnippet()
{
    if (!ensureSaved()) return;

    // Clear any search so the new snippet is visible in the list
    if (!m_searchEdit->text().isEmpty()) {
        m_searchTimer->stop();
        m_searchEdit->blockSignals(true);
        m_searchEdit->clear();
        m_searchEdit->blockSignals(false);
        m_model->search(QString());
    }
    
    int id = Database::instance().addSnippet("New Snippet", "");
    if (id == -1) {
        MessageBox::warning(this, "Add Snippet", "The snippet could not be added.");
        return;
    }

    m_model->refresh();
    selectSnippet(id);
    loadSnippet(id);
    setContentEditable(true);
    updateButtonStates();
    m_titleEdit->selectAll();
    m_titleEdit->setFocus();
}

void MainWindow::onEditSnippet()
{
    setContentEditable(true);
    m_contentEdit->setFocus();
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
    if (m_currentSnippetId == -1) return;

    QString title = m_titleEdit->text().trimmed();
    if (!MessageBox::question(this, "Delete Snippet",
                              QString("Are you sure you want to delete '%1'?").arg(title))) {
        return;
    }

    QString error;
    if (!Database::instance().deleteSnippet(m_currentSnippetId, &error)) {
        MessageBox::warning(this, "Delete Snippet", "The snippet could not be deleted.\n\n" + error);
        return;
    }
    m_isModified = false;
    m_model->refresh();
    selectSnippet(-1);
    loadSnippet(-1);
}

void MainWindow::onCopySnippet()
{
    copyToClipboard(m_contentEdit->toPlainText());
    statusBar()->showMessage("Content copied to clipboard", 2000);
}

void MainWindow::onTitleChanged()
{
    m_isModified = true;
    updateButtonStates();
}

void MainWindow::onContentChanged()
{
    m_isModified = true;
    updateButtonStates();
}

void MainWindow::onShowSettings()
{
    SettingsDialog dialog(this);
    dialog.setSavePendingChanges([this]() { return ensureSaved(); });
    connect(&dialog, &SettingsDialog::clipboardSettingsChanged, this, [this]() {
        setContentEditable(isContentEditable()); // refreshes the hint above the content
    });
    connect(&dialog, &SettingsDialog::databaseChanged, this, &MainWindow::onDatabaseChanged);
    dialog.exec();
}

void MainWindow::onDatabaseChanged()
{
    m_isModified = false;
    m_model->refresh();
    selectSnippet(-1);
    loadSnippet(-1);
    statusBar()->showMessage("Using database " + QDir::toNativeSeparators(Database::instance().databasePath()), 5000);
}

void MainWindow::updateButtonStates()
{
    bool hasSelection = m_currentSnippetId != -1;
    bool hasContent = !m_contentEdit->toPlainText().isEmpty();
    
    m_editAction->setEnabled(hasSelection && !isContentEditable());
    m_deleteAction->setEnabled(hasSelection);
    m_detailsAction->setEnabled(hasSelection);
    m_saveAction->setEnabled(m_isModified && hasSelection);
    m_copyAction->setEnabled(hasContent);
    m_editButton->setEnabled(m_editAction->isEnabled());
    m_deleteButton->setEnabled(m_deleteAction->isEnabled());
    m_saveButton->setEnabled(m_saveAction->isEnabled());
    m_copyButton->setEnabled(m_copyAction->isEnabled());
}

bool MainWindow::saveCurrentSnippet(QString *errorMessage)
{
    if (!m_isModified || m_currentSnippetId == -1) {
        return true;
    }

    Snippet snippet = m_current;
    snippet.title = m_titleEdit->text().trimmed();
    snippet.content = m_contentEdit->toPlainText();

    if (snippet.title.isEmpty()) {
        snippet.title = "Untitled";
    }

    if (!Database::instance().updateSnippet(snippet, errorMessage)) {
        statusBar()->showMessage("The snippet could not be saved");
        return false;
    }

    m_current = snippet;
    m_isModified = false;
    m_model->refresh();
    selectSnippet(m_currentSnippetId);
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

    QString title = m_titleEdit->text().trimmed();
    MessageBox::warning(this, "Save Snippet",
                        QString("Your changes to '%1' could not be saved.\n\n%2\n\n"
                                "They are still in the editor. Check that the database location in "
                                "File > Settings... is available and writable, then try saving again.")
                            .arg(title.isEmpty() ? "Untitled" : title, error));
    return false;
}
