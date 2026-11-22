#ifndef SNIPPETMODEL_H
#define SNIPPETMODEL_H

#include <QAbstractItemModel>
#include <QVector>
#include "database.h"

// The snippet list: a tree of folders and snippets, or a flat list while
// searching, filtering by tag, showing the trash, or when set to flat
class SnippetModel : public QAbstractItemModel
{
    Q_OBJECT

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,  // the snippet's id, or -1 for a folder
        TitleRole,
        ContentRole,
        CreatedRole,
        ModifiedRole,
        // The first line of the content, as plain text
        PreviewRole,
        // The folder's id, for folder rows (-1 for snippets)
        FolderIdRole
    };

    explicit SnippetModel(QObject *parent = nullptr);

    QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex &child) const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Dragging snippets and folders into folders
    QStringList mimeTypes() const override;
    QMimeData *mimeData(const QModelIndexList &indexes) const override;
    Qt::DropActions supportedDropActions() const override;
    bool canDropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column,
                         const QModelIndex &parent) const override;
    bool dropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column,
                      const QModelIndex &parent) override;

    void refresh();
    void search(const QString &searchTerm);
    void setSortOrder(Database::SortOrder order);
    // Only show snippets with this tag (or without tags, if untagged is true)
    void setTagFilter(const QString &tag, bool untagged);
    // Show the snippets in the trash instead of the others
    void setShowTrash(bool trash);
    bool showsTrash() const { return m_filter.trash; }
    // Always show a flat list, without folders (for the quick search)
    void setFlat(bool flat);
    // Whether folders are shown right now
    bool showsFolders() const;
    Database::SortOrder sortOrder() const;

    QModelIndex indexForSnippet(int id) const;
    QModelIndex indexForFolder(int id) const;
    // Every snippet in the model, in display order
    QList<Snippet> snippets() const;
    // The snippet to select after this one goes: the next in its folder, else the previous
    int neighbourOf(int snippetId) const;
    bool isFolder(const QModelIndex &index) const;

signals:
    // Something was dropped into a folder; the list needs refreshing
    void itemsMoved(const QString &errorMessage);

private:
    struct Node {
        bool folder = false;
        int id = -1;       // snippet or folder id
        int parent = -1;   // index of the parent node in m_nodes, -1 for top level
        int row = 0;       // position within the parent
        QList<int> children;
        Folder folderInfo;
        Snippet snippet;
    };
    const Node *node(const QModelIndex &index) const;
    QModelIndex indexForNode(int nodeIndex) const;

    QVector<Node> m_nodes;
    QList<int> m_topLevel;
    SnippetFilter m_filter;
    bool m_flat = false;
    mutable QHash<int, QString> m_previews;
    Database::SortOrder m_sortOrder = Database::CreatedDescending;
};

#endif // SNIPPETMODEL_H
