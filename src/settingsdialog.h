#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <QDialog>

class QLineEdit;

class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget *parent = nullptr);

signals:
    // Emitted just before the active database is replaced, so pending edits can be saved
    void aboutToChangeDatabase();
    // Emitted after a different database file has become the active one
    void databaseChanged();

private slots:
    void onMoveDatabase();
    void onOpenDatabase();
    void onCreateDatabase();

private:
    void updatePathDisplay();
    QString startDirectory() const;

    QLineEdit *m_pathEdit;
};

#endif // SETTINGSDIALOG_H
