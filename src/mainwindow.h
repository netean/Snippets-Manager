#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include "snippetmodel.h"

class GlobalShortcut;
class QAction;
class QSystemTrayIcon;
class QuickSearch;
class QComboBox;
class QLabel;
class QLineEdit;
class QListView;
class QPushButton;
class QSplitter;
class QTimer;
class SnippetEditor;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    // Handles a command from the command line or another launch:
    // "show", or "popup [activation token]" for the quick search
    void handleCommand(const QString &command);
    // Whether the app can keep running without its window (in the tray)
    bool canRunInBackground() const;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onSelectionChanged(const QModelIndex &current);
    void onSearchTextChanged();
    void onSortOrderChanged();
    void onTagFilterChanged();
    void onAddSnippet();
    void onEditSnippet();
    void onSaveSnippet();
    void onDeleteSnippet();
    void onCopySnippet();
    void onEditorChanged();
    void onShowSettings();
    void onDatabaseChanged();
    void onShowDetails();
    void onRestoreSnippet();
    void onUndoDelete();
    void onEmptyTrash();
    void onImport();
    void onExport();
    void onQuit();
    void applyQuickAccessSettings();
    void showWindow();
    void showQuickSearch(const QString &activationToken = QString());
    void openSnippet(int id);
    void onAbout();

private:
    void setupUI();
    void setupActions();
    void setupMenus();
    void updateButtonStates();
    void updateModeLabel();
    // Repopulates the tag filter and the tag suggestions, keeping the selection
    void refreshTags();
    // Saves pending edits; returns true if there was nothing to save or the save worked
    bool saveCurrentSnippet(QString *errorMessage = nullptr);
    // Like saveCurrentSnippet, but tells the user if the save failed
    bool ensureSaved();
    void loadSnippet(int id);
    void selectSnippet(int id);
    void setContentEditable(bool editable);
    bool inTrashView() const;
    bool copyOnSelect() const;
    int currentId() const;

    QSplitter *m_splitter = nullptr;

    // Left panel
    QLineEdit *m_searchEdit = nullptr;
    QComboBox *m_tagFilterCombo = nullptr;
    QComboBox *m_sortCombo = nullptr;
    QListView *m_snippetList = nullptr;
    QPushButton *m_addButton = nullptr;
    QPushButton *m_editButton = nullptr;
    QPushButton *m_deleteButton = nullptr;
    QPushButton *m_restoreButton = nullptr;

    // Right panel
    QLabel *m_modeLabel = nullptr;
    SnippetEditor *m_editor = nullptr;
    QPushButton *m_saveButton = nullptr;
    QPushButton *m_copyButton = nullptr;

    QAction *m_newAction = nullptr;
    QAction *m_saveAction = nullptr;
    QAction *m_editAction = nullptr;
    QAction *m_copyAction = nullptr;
    QAction *m_deleteAction = nullptr;
    QAction *m_findAction = nullptr;
    QAction *m_detailsAction = nullptr;
    QAction *m_restoreAction = nullptr;
    QAction *m_undoDeleteAction = nullptr;
    QAction *m_emptyTrashAction = nullptr;

    SnippetModel *m_model = nullptr;
    bool m_updatingSelection = false;
    // Snippets moved to the trash, most recent last, for Undo Delete
    QList<int> m_recentlyTrashed;

    QSystemTrayIcon *m_tray = nullptr;
    QuickSearch *m_quickSearch = nullptr;
    GlobalShortcut *m_globalShortcut = nullptr;
    // Set by File > Quit, so closing the window really quits
    bool m_quitting = false;
    QTimer *m_searchTimer = nullptr;
};

#endif // MAINWINDOW_H
