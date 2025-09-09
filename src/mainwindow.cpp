#include "mainwindow.h"
#include "database.h"
#include <QApplication>
#include <QClipboard>
#include <QMessageBox>
#include <QHeaderView>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_currentSnippetId(-1)
    , m_isModified(false)
{
    setupUI();
    
    m_model = new SnippetModel(this);
    m_snippetList->setModel(m_model);
    
    m_searchTimer = new QTimer(this);
    m_searchTimer->setSingleShot(true);
    m_searchTimer->setInterval(300);
    connect(m_searchTimer, &QTimer::timeout, this, &MainWindow::onSearchTextChanged);
    
    // Connect signals
    connect(m_snippetList->selectionModel(), &QItemSelectionModel::currentChanged,
            this, &MainWindow::onSelectionChanged);
    connect(m_searchEdit, &QLineEdit::textChanged, [this]() { m_searchTimer->start(); });
    connect(m_addButton, &QPushButton::clicked, this, &MainWindow::onAddSnippet);
    connect(m_deleteButton, &QPushButton::clicked, this, &MainWindow::onDeleteSnippet);
    connect(m_saveButton, &QPushButton::clicked, this, &MainWindow::onSaveSnippet);
    connect(m_copyButton, &QPushButton::clicked, this, &MainWindow::onCopySnippet);
    connect(m_titleEdit, &QLineEdit::textChanged, this, &MainWindow::onTitleChanged);
    connect(m_contentEdit, &QTextEdit::textChanged, this, &MainWindow::onContentChanged);
    
    updateButtonStates();
    
    setWindowTitle("Snippet Manager");
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
    
    m_snippetList = new QListView;
    leftLayout->addWidget(m_snippetList);
    
    QHBoxLayout *leftButtonLayout = new QHBoxLayout;
    m_addButton = new QPushButton("Add");
    m_deleteButton = new QPushButton("Delete");
    leftButtonLayout->addWidget(m_addButton);
    leftButtonLayout->addWidget(m_deleteButton);
    leftLayout->addLayout(leftButtonLayout);
    
    // Right panel
    m_rightPanel = new QWidget;
    QVBoxLayout *rightLayout = new QVBoxLayout(m_rightPanel);
    
    rightLayout->addWidget(new QLabel("Title:"));
    m_titleEdit = new QLineEdit;
    rightLayout->addWidget(m_titleEdit);
    
    rightLayout->addWidget(new QLabel("Content:"));
    m_contentEdit = new QTextEdit;
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

void MainWindow::onSelectionChanged()
{
    saveCurrentSnippet();
    
    QModelIndex current = m_snippetList->currentIndex();
    if (current.isValid()) {
        Snippet snippet = m_model->getSnippet(current.row());
        m_currentSnippetId = snippet.id;
        m_titleEdit->setText(snippet.title);
        m_contentEdit->setPlainText(snippet.content);
        m_isModified = false;
    } else {
        m_currentSnippetId = -1;
        m_titleEdit->clear();
        m_contentEdit->clear();
        m_isModified = false;
    }
    
    updateButtonStates();
}

void MainWindow::onSearchTextChanged()
{
    saveCurrentSnippet();
    m_model->search(m_searchEdit->text());
}

void MainWindow::onAddSnippet()
{
    saveCurrentSnippet();
    
    QString title = "New Snippet";
    QString content = "";
    
    if (Database::instance().addSnippet(title, content)) {
        m_model->refresh();
        
        // Select the new snippet (it should be at the top)
        QModelIndex firstIndex = m_model->index(0, 0);
        m_snippetList->setCurrentIndex(firstIndex);
        m_titleEdit->selectAll();
        m_titleEdit->setFocus();
    }
}

void MainWindow::onSaveSnippet()
{
    saveCurrentSnippet();
}

void MainWindow::onDeleteSnippet()
{
    QModelIndex current = m_snippetList->currentIndex();
    if (!current.isValid()) return;
    
    Snippet snippet = m_model->getSnippet(current.row());
    
    int ret = QMessageBox::question(this, "Delete Snippet",
                                   QString("Are you sure you want to delete '%1'?").arg(snippet.title),
                                   QMessageBox::Yes | QMessageBox::No);
    
    if (ret == QMessageBox::Yes) {
        if (Database::instance().deleteSnippet(snippet.id)) {
            m_model->refresh();
            m_currentSnippetId = -1;
            m_titleEdit->clear();
            m_contentEdit->clear();
            m_isModified = false;
            updateButtonStates();
        }
    }
}

void MainWindow::onCopySnippet()
{
    QClipboard *clipboard = QApplication::clipboard();
    clipboard->setText(m_contentEdit->toPlainText());
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

void MainWindow::updateButtonStates()
{
    bool hasSelection = m_currentSnippetId != -1;
    bool hasContent = !m_contentEdit->toPlainText().isEmpty();
    
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
            updateButtonStates();
        }
    }
}