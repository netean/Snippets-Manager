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
    void setSortOrder(Database::SortOrder order);
    // Only show snippets with this tag (or without tags, if untagged is true)
    void setTagFilter(const QString &tag, bool untagged);
    // Show the snippets in the trash instead of the others
    void setShowTrash(bool trash);
    bool showsTrash() const { return m_filter.trash; }
    Database::SortOrder sortOrder() const;
    Snippet getSnippet(int row) const;
    int rowForId(int id) const;

private:
    QList<Snippet> m_snippets;
    SnippetFilter m_filter;
    Database::SortOrder m_sortOrder = Database::CreatedDescending;
};

#endif // SNIPPETMODEL_H