#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QListView>
#include <QTextEdit>
#include <QLineEdit>
#include <QPushButton>
#include <QSplitter>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QTimer>
#include "snippetmodel.h"

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void onSelectionChanged();
    void onSearchTextChanged();
    void onAddSnippet();
    void onSaveSnippet();
    void onDeleteSnippet();
    void onCopySnippet();
    void onTitleChanged();
    void onContentChanged();

private:
    void setupUI();
    void updateButtonStates();
    void saveCurrentSnippet();
    
    QWidget *m_centralWidget;
    QSplitter *m_splitter;
    
    // Left panel
    QWidget *m_leftPanel;
    QLineEdit *m_searchEdit;
    QListView *m_snippetList;
    QPushButton *m_addButton;
    QPushButton *m_deleteButton;
    
    // Right panel
    QWidget *m_rightPanel;
    QLineEdit *m_titleEdit;
    QTextEdit *m_contentEdit;
    QPushButton *m_saveButton;
    QPushButton *m_copyButton;
    
    SnippetModel *m_model;
    int m_currentSnippetId;
    bool m_isModified;
    QTimer *m_searchTimer;
};

#endif // MAINWINDOW_H