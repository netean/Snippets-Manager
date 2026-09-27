#include "mainwindow.h"
#include "database.h"
#include "settingsdialog.h"
#include <QApplication>
#include <QClipboard>
#include <QMessageBox>
#include <QHeaderView>
#include <QStatusBar>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QMouseEvent>
#include <QSettings>
#include <QIcon>
#include <QDebug>
#include <QDir>
#include <QCoreApplication>
#include <QFile>

static const char *SortOrderKey = "view/sortOrder";

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_currentSnippetId(-1)
    , m_isModified(false)
    , m_updatingSelection(false)
{
    setupUI();
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
    connect(m_searchEdit, &QLineEdit::textChanged, [this]() { m_searchTimer->start(); });
    connect(m_sortCombo, &QComboBox::currentIndexChanged, this, &MainWindow::onSortOrderChanged);
    connect(m_addButton, &QPushButton::clicked, this, &MainWindow::onAddSnippet);
    connect(m_editButton, &QPushButton::clicked, this, &MainWindow::onEditSnippet);
    connect(m_deleteButton, &QPushButton::clicked, this, &MainWindow::onDeleteSnippet);
    connect(m_saveButton, &QPushButton::clicked, this, &MainWindow::onSaveSnippet);
    connect(m_copyButton, &QPushButton::clicked, this, &MainWindow::onCopySnippet);
    connect(m_titleEdit, &QLineEdit::textChanged, this, &MainWindow::onTitleChanged);
    connect(m_titleEdit, &QLineEdit::returnPressed, this, &MainWindow::saveCurrentSnippet);
    connect(m_contentEdit, &QTextEdit::textChanged, this, &MainWindow::onContentChanged);

    // Clicking in the content area copies it to the clipboard
    m_contentEdit->viewport()->installEventFilter(this);
    
    loadSnippet(-1);
    
    setWindowTitle("Snippet Manager - Text & Code Snippets");
    
    // The application icon is set in main.cpp, but we can override for this window if needed
    QIcon windowIcon = QApplication::windowIcon();
    if (!windowIcon.isNull()) {
        setWindowIcon(windowIcon);
        qDebug() << "Window icon inherited from application";
    } else {
        // Fallback icon loading if application icon wasn't set
        QIcon icon(":/icon.png");
        if (icon.isNull()) {
            icon = QIcon(":/icon.svg");
        }
        if (!icon.isNull()) {
            setWindowIcon(icon);
            qDebug() << "Window icon set from resources";
        }
    }
    
    resize(800, 600);
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
    m_searchEdit->setPlaceholderText("Search snippets...");
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
    m_splitter->setSizes({300, 500});
    
    QHBoxLayout *mainLayout = new QHBoxLayout(m_centralWidget);
    mainLayout->addWidget(m_splitter);
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
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    // While viewing (not editing) a snippet, a click anywhere in the content
    // copies it. If the user dragged to select part of the text, only the
    // selection is copied. Clicks while editing are left alone so the
    // clipboard isn't overwritten before the user can paste into the snippet.
    if (watched == m_contentEdit->viewport() && event->type() == QEvent::MouseButtonRelease) {
        QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() == Qt::LeftButton && m_currentSnippetId != -1 && !isContentEditable()) {
            QString selected = m_contentEdit->textCursor().selectedText();
            if (!selected.isEmpty()) {
                // QTextCursor uses the Unicode paragraph separator for line breaks
                selected.replace(QChar::ParagraphSeparator, '\n');
                QApplication::clipboard()->setText(selected);
                statusBar()->showMessage("Selected text copied to clipboard", 2000);
            } else if (!m_contentEdit->toPlainText().isEmpty()) {
                QApplication::clipboard()->setText(m_contentEdit->toPlainText());
                statusBar()->showMessage("Content copied to clipboard", 2000);
            }
        }
    }
    return QMainWindow::eventFilter(watched, event);
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
    } else {
        m_modeLabel->setText("Viewing snippet - Click the content to copy it");
    }
}

bool MainWindow::isContentEditable() const
{
    return !m_contentEdit->isReadOnly();
}

void MainWindow::loadSnippet(int id)
{
    Snippet snippet = id != -1 ? Database::instance().getSnippet(id) : Snippet();
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

    // Saving refreshes the model, which would lose the new selection, so restore it afterwards
    saveCurrentSnippet();
    selectSnippet(newId);
    loadSnippet(newId);

    if (m_currentSnippetId != -1) {
        // Automatically copy content to clipboard when selecting a snippet
        QApplication::clipboard()->setText(m_contentEdit->toPlainText());
        statusBar()->showMessage("Content copied to clipboard - Rename using the title field, click 'Edit' to change the content", 4000);
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
    saveCurrentSnippet();

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
        QMessageBox::warning(this, "Add Snippet", "The snippet could not be added.");
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
    saveCurrentSnippet();
    setContentEditable(false);
    updateButtonStates();
}

void MainWindow::onDeleteSnippet()
{
    if (m_currentSnippetId == -1) return;

    QString title = m_titleEdit->text().trimmed();
    int ret = QMessageBox::question(this, "Delete Snippet",
                                   QString("Are you sure you want to delete '%1'?").arg(title),
                                   QMessageBox::Yes | QMessageBox::No);
    
    if (ret == QMessageBox::Yes) {
        if (Database::instance().deleteSnippet(m_currentSnippetId)) {
            m_isModified = false;
            m_model->refresh();
            selectSnippet(-1);
            loadSnippet(-1);
        }
    }
}

void MainWindow::onCopySnippet()
{
    QClipboard *clipboard = QApplication::clipboard();
    clipboard->setText(m_contentEdit->toPlainText());
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
    connect(&dialog, &SettingsDialog::aboutToChangeDatabase, this, &MainWindow::saveCurrentSnippet);
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
    
    m_editButton->setEnabled(hasSelection && !isContentEditable());
    m_deleteButton->setEnabled(hasSelection);
    m_saveButton->setEnabled(m_isModified && hasSelection);
    m_copyButton->setEnabled(hasContent);
}

void MainWindow::saveCurrentSnippet()
{
    if (m_isModified && m_currentSnippetId != -1) {
        QString title = m_titleEdit->text().trimmed();
        QString content = m_contentEdit->toPlainText();
        
        if (title.isEmpty()) {
            title = "Untitled";
        }
        
        if (Database::instance().updateSnippet(m_currentSnippetId, title, content)) {
            m_isModified = false;
            m_model->refresh();
            selectSnippet(m_currentSnippetId);
            updateButtonStates();
            statusBar()->showMessage("Snippet saved successfully", 2000);
        }
    }
}
