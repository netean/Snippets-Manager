#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <QDialog>
#include <functional>

class GlobalShortcut;
class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTabWidget;

// QSettings keys shared between the settings dialog and the main window
namespace SettingsKeys {
// Copy a snippet when it's selected, and when its content is clicked
inline constexpr char CopyOnSelect[] = "clipboard/copyOnSelect";
// Ask clipboard managers (e.g. KDE's Klipper) not to keep copied snippets in their history
inline constexpr char HideFromHistory[] = "clipboard/hideFromHistory";
// Fill in placeholders like {{date}} when copying
inline constexpr char Placeholders[] = "clipboard/placeholders";
// Rich text snippets and the formatting toolbar; when off, everything is plain text
inline constexpr char RichText[] = "editor/richText";
// Colour code snippets by their language (when the build supports it)
inline constexpr char SyntaxHighlighting[] = "editor/syntaxHighlighting";
// Show a tray icon, and keep running there when the window is closed
inline constexpr char TrayIcon[] = "quickAccess/trayIcon";
// Open the quick search with a system-wide keyboard shortcut
inline constexpr char GlobalShortcut[] = "quickAccess/globalShortcut";
}

class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget *parent = nullptr);

    // Called just before the active database is replaced, so pending edits can
    // be saved. If it returns false the database is left as it is.
    void setSavePendingChanges(std::function<bool()> savePendingChanges);
    // The active global shortcut (or null), to show which keys it uses
    void setGlobalShortcut(GlobalShortcut *shortcut);

signals:
    // Emitted after a different database file has become the active one
    void databaseChanged();
    // Emitted when one of the clipboard options is changed
    void clipboardSettingsChanged();
    // Emitted when one of the editor options is changed
    void editorSettingsChanged();
    // Emitted when the tray or global shortcut options are changed
    void quickAccessSettingsChanged();

private slots:
    void onMoveDatabase();
    void onOpenDatabase();
    void onCreateDatabase();

private:
    void updatePathDisplay();
    QString startDirectory() const;
    bool savePendingChanges();

    void updateShortcutStatus();

    QLineEdit *m_pathEdit;
    QTabWidget *m_tabs;
    QLabel *m_shortcutStatus = nullptr;
    QPushButton *m_changeShortcutButton = nullptr;
    GlobalShortcut *m_globalShortcut = nullptr;
    std::function<bool()> m_savePendingChanges;
};

#endif // SETTINGSDIALOG_H
