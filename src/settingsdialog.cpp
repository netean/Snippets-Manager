#include "settingsdialog.h"
#include "database.h"
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
#include <QMessageBox>
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

    QDialogButtonBox *buttonBox = new QDialogButtonBox(QDialogButtonBox::Close);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->addWidget(databaseGroup);
    mainLayout->addStretch();
    mainLayout->addWidget(buttonBox);

    connect(showFolderButton, &QPushButton::clicked, this, [this]() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(m_pathEdit->text()).absolutePath()));
    });
    connect(moveButton, &QPushButton::clicked, this, &SettingsDialog::onMoveDatabase);
    connect(openButton, &QPushButton::clicked, this, &SettingsDialog::onOpenDatabase);
    connect(createButton, &QPushButton::clicked, this, &SettingsDialog::onCreateDatabase);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    updatePathDisplay();
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

    emit aboutToChangeDatabase();

    QString error;
    if (!Database::instance().moveDatabase(newPath, &error)) {
        QMessageBox::warning(this, "Move Database", "The database could not be moved.\n\n" + error);
        return;
    }

    updatePathDisplay();
    emit databaseChanged();
}

void SettingsDialog::onOpenDatabase()
{
    QString path = QFileDialog::getOpenFileName(this, "Open Database", startDirectory(), DatabaseFileFilter);
    if (path.isEmpty()) return;

    emit aboutToChangeDatabase();

    QString error;
    if (!Database::instance().openDatabase(path, &error)) {
        QMessageBox::warning(this, "Open Database", "The database could not be opened.\n\n" + error);
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
            QMessageBox::question(this, "Create New Database",
                                  QString("'%1' already exists. Replace it?").arg(path)) != QMessageBox::Yes) {
            return;
        }
    }

    emit aboutToChangeDatabase();

    QString error;
    if (!Database::instance().createDatabase(path, &error)) {
        QMessageBox::warning(this, "Create New Database", "The database could not be created.\n\n" + error);
        return;
    }

    updatePathDisplay();
    emit databaseChanged();
}
