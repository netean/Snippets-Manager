#ifndef SNIPPETMODEL_H
#define SNIPPETMODEL_H

#include <QAbstractListModel>
#include "database.h"

class SnippetModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        TitleRole,
        ContentRole,
        CreatedRole,
        ModifiedRole
    };

    explicit SnippetModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    void refresh();
    void search(const QString &searchTerm);
    Snippet getSnippet(int row) const;

private:
    QList<Snippet> m_snippets;
};

#endif // SNIPPETMODEL_H