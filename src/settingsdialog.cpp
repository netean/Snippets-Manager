#include "settingsdialog.h"
#include "database.h"
#include "messagebox.h"
#include <QCheckBox>
#include <QSettings>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QGroupBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QDesktopServices>
#include <QUrl>

static const char *DatabaseFileFilter = "SQLite databases (*.db *.sqlite *.sqlite3);;All files (*)";

SettingsDialog::SettingsDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("Settings");

    QGroupBox *databaseGroup = new QGroupBox("Database");
    QVBoxLayout *groupLayout = new QVBoxLayout(databaseGroup);

    groupLayout->addWidget(new QLabel("Current database location:"));

    QHBoxLayout *pathLayout = new QHBoxLayout;
    m_pathEdit = new QLineEdit;
    m_pathEdit->setReadOnly(true);
    m_pathEdit->setMinimumWidth(400);
    QPushButton *showFolderButton = new QPushButton("Show Folder");
    showFolderButton->setToolTip("Open the folder containing the database");
    pathLayout->addWidget(m_pathEdit);
    pathLayout->addWidget(showFolderButton);
    groupLayout->addLayout(pathLayout);

    QHBoxLayout *actionLayout = new QHBoxLayout;
    QPushButton *moveButton = new QPushButton("Move...");
    moveButton->setToolTip("Move the current database file to a new location");
    QPushButton *openButton = new QPushButton("Open Existing...");
    openButton->setToolTip("Switch to a different snippets database");
    QPushButton *createButton = new QPushButton("Create New...");
    createButton->setToolTip("Create a new, empty snippets database and switch to it");
    actionLayout->addWidget(moveButton);
    actionLayout->addWidget(openButton);
    actionLayout->addWidget(createButton);
    actionLayout->addStretch();
    groupLayout->addLayout(actionLayout);

    QGroupBox *clipboardGroup = new QGroupBox("Clipboard");
    QVBoxLayout *clipboardLayout = new QVBoxLayout(clipboardGroup);
    QSettings settings;

    QCheckBox *copyOnSelectCheck = new QCheckBox("Copy a snippet when it's selected or its content is clicked");
    copyOnSelectCheck->setToolTip("When off, snippets are only copied with the Copy button");
    copyOnSelectCheck->setChecked(settings.value(SettingsKeys::CopyOnSelect, true).toBool());
    clipboardLayout->addWidget(copyOnSelectCheck);

    QCheckBox *hideFromHistoryCheck = new QCheckBox("Keep copied snippets out of clipboard history");
    hideFromHistoryCheck->setToolTip("Marks copied snippets as sensitive, so clipboard managers that respect this "
                                     "(such as KDE's Klipper) don't save them. Useful if you store passwords or tokens.");
    hideFromHistoryCheck->setChecked(settings.value(SettingsKeys::HideFromHistory, false).toBool());
    clipboardLayout->addWidget(hideFromHistoryCheck);

    QDialogButtonBox *buttonBox = new QDialogButtonBox(QDialogButtonBox::Close);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->addWidget(databaseGroup);
    mainLayout->addWidget(clipboardGroup);
    mainLayout->addStretch();
    mainLayout->addWidget(buttonBox);

    connect(showFolderButton, &QPushButton::clicked, this, [this]() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(m_pathEdit->text()).absolutePath()));
    });
    connect(moveButton, &QPushButton::clicked, this, &SettingsDialog::onMoveDatabase);
    connect(openButton, &QPushButton::clicked, this, &SettingsDialog::onOpenDatabase);
    connect(createButton, &QPushButton::clicked, this, &SettingsDialog::onCreateDatabase);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(copyOnSelectCheck, &QCheckBox::toggled, this, [this](bool checked) {
        QSettings().setValue(SettingsKeys::CopyOnSelect, checked);
        emit clipboardSettingsChanged();
    });
    connect(hideFromHistoryCheck, &QCheckBox::toggled, this, [this](bool checked) {
        QSettings().setValue(SettingsKeys::HideFromHistory, checked);
        emit clipboardSettingsChanged();
    });

    updatePathDisplay();
}

void SettingsDialog::setSavePendingChanges(std::function<bool()> savePendingChanges)
{
    m_savePendingChanges = std::move(savePendingChanges);
}

bool SettingsDialog::savePendingChanges()
{
    return !m_savePendingChanges || m_savePendingChanges();
}

void SettingsDialog::updatePathDisplay()
{
    m_pathEdit->setText(QDir::toNativeSeparators(Database::instance().databasePath()));
    m_pathEdit->setCursorPosition(0);
}

QString SettingsDialog::startDirectory() const
{
    return QFileInfo(Database::instance().databasePath()).absolutePath();
}

void SettingsDialog::onMoveDatabase()
{
    QString currentPath = Database::instance().databasePath();
    QString suggested = startDirectory() + "/" + QFileInfo(currentPath).fileName();
    QString newPath = QFileDialog::getSaveFileName(this, "Move Database To", suggested, DatabaseFileFilter);
    if (newPath.isEmpty()) return;

    if (!savePendingChanges()) return;

    QString error;
    if (!Database::instance().moveDatabase(newPath, &error)) {
        MessageBox::warning(this, "Move Database", "The database could not be moved.\n\n" + error);
        return;
    }

    updatePathDisplay();
    emit databaseChanged();
}

void SettingsDialog::onOpenDatabase()
{
    QString path = QFileDialog::getOpenFileName(this, "Open Database", startDirectory(), DatabaseFileFilter);
    if (path.isEmpty()) return;

    if (!savePendingChanges()) return;

    QString error;
    if (!Database::instance().openDatabase(path, &error)) {
        MessageBox::warning(this, "Open Database", "The database could not be opened.\n\n" + error);
        return;
    }

    updatePathDisplay();
    emit databaseChanged();
}

void SettingsDialog::onCreateDatabase()
{
    QString path = QFileDialog::getSaveFileName(this, "Create New Database",
                                                startDirectory() + "/snippets-new.db", DatabaseFileFilter);
    if (path.isEmpty()) return;

    if (QFileInfo(path).suffix().isEmpty()) {
        path += ".db";
        if (QFileInfo::exists(path) &&
            !MessageBox::question(this, "Create New Database",
                                  QString("'%1' already exists. Replace it?").arg(QDir::toNativeSeparators(path)))) {
            return;
        }
    }

    if (!savePendingChanges()) return;

    QString error;
    if (!Database::instance().createDatabase(path, &error)) {
        MessageBox::warning(this, "Create New Database", "The database could not be created.\n\n" + error);
        return;
    }

    updatePathDisplay();
    emit databaseChanged();
}
