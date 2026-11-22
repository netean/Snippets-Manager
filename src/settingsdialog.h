#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <QDialog>
#include <functional>

class QLineEdit;

// QSettings keys shared between the settings dialog and the main window
namespace SettingsKeys {
// Copy a snippet when it's selected, and when its content is clicked
inline constexpr char CopyOnSelect[] = "clipboard/copyOnSelect";
// Ask clipboard managers (e.g. KDE's Klipper) not to keep copied snippets in their history
inline constexpr char HideFromHistory[] = "clipboard/hideFromHistory";
}

class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget *parent = nullptr);

    // Called just before the active database is replaced, so pending edits can
    // be saved. If it returns false the database is left as it is.
    void setSavePendingChanges(std::function<bool()> savePendingChanges);

signals:
    // Emitted after a different database file has become the active one
    void databaseChanged();
    // Emitted when one of the clipboard options is changed
    void clipboardSettingsChanged();

private slots:
    void onMoveDatabase();
    void onOpenDatabase();
    void onCreateDatabase();

private:
    void updatePathDisplay();
    QString startDirectory() const;
    bool savePendingChanges();

    QLineEdit *m_pathEdit;
    std::function<bool()> m_savePendingChanges;
};

#endif // SETTINGSDIALOG_H
