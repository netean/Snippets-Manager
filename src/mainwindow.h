#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QListView>
#include <QTextEdit>
#include <QLineEdit>
#include <QPushButton>
#include <QComboBox>
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

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void onSelectionChanged(const QModelIndex &current);
    void onSearchTextChanged();
    void onSortOrderChanged();
    void onAddSnippet();
    void onEditSnippet();
    void onSaveSnippet();
    void onDeleteSnippet();
    void onCopySnippet();
    void onTitleChanged();
    void onContentChanged();
    void onShowSettings();
    void onDatabaseChanged();

private:
    void setupUI();
    void setupMenus();
    void updateButtonStates();
    void saveCurrentSnippet();
    void loadSnippet(int id);
    void selectSnippet(int id);
    void setContentEditable(bool editable);
    bool isContentEditable() const;
    
    QWidget *m_centralWidget;
    QSplitter *m_splitter;
    
    // Left panel
    QWidget *m_leftPanel;
    QLineEdit *m_searchEdit;
    QComboBox *m_sortCombo;
    QListView *m_snippetList;
    QPushButton *m_addButton;
    QPushButton *m_editButton;
    QPushButton *m_deleteButton;
    
    // Right panel
    QWidget *m_rightPanel;
    QLabel *m_modeLabel;
    QLineEdit *m_titleEdit;
    QTextEdit *m_contentEdit;
    QPushButton *m_saveButton;
    QPushButton *m_copyButton;
    
    SnippetModel *m_model;
    int m_currentSnippetId;
    bool m_isModified;
    bool m_updatingSelection;
    QTimer *m_searchTimer;
};

#endif // MAINWINDOW_H
