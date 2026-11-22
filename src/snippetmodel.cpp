#include "snippetmodel.h"
#include "richtext.h"
#include <QDataStream>
#include <QIODevice>
#include <QApplication>
#include <QIcon>
#include <QStyle>
#include <QMimeData>
#include <QTimer>

static const char *ItemsMimeType = "application/x-snippetmanager-items";

SnippetModel::SnippetModel(QObject *parent)
    : QAbstractItemModel(parent)
{
    refresh();
}

bool SnippetModel::showsFolders() const
{
    return !m_flat && m_filter.search.trimmed().isEmpty() && m_filter.tag.isEmpty()
           && !m_filter.untagged && !m_filter.trash;
}

void SnippetModel::refresh()
{
    beginResetModel();
    m_previews.clear();
    m_nodes.clear();
    m_topLevel.clear();

    const QList<Snippet> snippets = Database::instance().snippets(m_filter, m_sortOrder);
    if (showsFolders()) {
        // Folders first (sorted by name), then snippets in the chosen order
        QHash<int, int> folderNodes;
        const QList<Folder> folders = Database::instance().folders();
        for (const Folder &folder : folders) {
            Node node;
            node.folder = true;
            node.id = folder.id;
            node.folderInfo = folder;
            folderNodes.insert(folder.id, m_nodes.size());
            m_nodes.append(node);
        }
        for (const Folder &folder : folders) {
            int nodeIndex = folderNodes.value(folder.id);
            int parentNode = folderNodes.value(folder.parentId, -1);
            m_nodes[nodeIndex].parent = parentNode;
        }
        for (const Snippet &snippet : snippets) {
            Node node;
            node.id = snippet.id;
            node.snippet = snippet;
            node.parent = folderNodes.value(snippet.folderId, -1);
            m_nodes.append(node);
        }
        // Link children to parents, keeping the order above
        for (int i = 0; i < m_nodes.size(); ++i) {
            QList<int> &siblings = m_nodes[i].parent == -1 ? m_topLevel : m_nodes[m_nodes[i].parent].children;
            m_nodes[i].row = siblings.size();
            siblings.append(i);
        }
    } else {
        for (const Snippet &snippet : snippets) {
            Node node;
            node.id = snippet.id;
            node.snippet = snippet;
            node.row = m_topLevel.size();
            m_topLevel.append(m_nodes.size());
            m_nodes.append(node);
        }
    }
    endResetModel();
}

const SnippetModel::Node *SnippetModel::node(const QModelIndex &index) const
{
    if (!index.isValid() || index.internalId() >= quintptr(m_nodes.size())) return nullptr;
    return &m_nodes.at(int(index.internalId()));
}

QModelIndex SnippetModel::indexForNode(int nodeIndex) const
{
    if (nodeIndex < 0 || nodeIndex >= m_nodes.size()) return QModelIndex();
    return createIndex(m_nodes.at(nodeIndex).row, 0, quintptr(nodeIndex));
}

QModelIndex SnippetModel::index(int row, int column, const QModelIndex &parent) const
{
    if (column != 0 || row < 0) return QModelIndex();
    const QList<int> &siblings = parent.isValid() ? node(parent)->children : m_topLevel;
    if (row >= siblings.size()) return QModelIndex();
    return createIndex(row, 0, quintptr(siblings.at(row)));
}

QModelIndex SnippetModel::parent(const QModelIndex &child) const
{
    const Node *n = node(child);
    return n ? indexForNode(n->parent) : QModelIndex();
}

int SnippetModel::rowCount(const QModelIndex &parent) const
{
    if (parent.column() > 0) return 0;
    if (!parent.isValid()) return m_topLevel.size();
    return node(parent)->children.size();
}

int SnippetModel::columnCount(const QModelIndex &) const
{
    return 1;
}

bool SnippetModel::isFolder(const QModelIndex &index) const
{
    const Node *n = node(index);
    return n && n->folder;
}

QVariant SnippetModel::data(const QModelIndex &index, int role) const
{
    const Node *n = node(index);
    if (!n) return QVariant();

    if (n->folder) {
        switch (role) {
        case Qt::DisplayRole:
        case TitleRole:
            return n->folderInfo.name;
        case Qt::DecorationRole:
            // Qt's own folder icon where the desktop's icon theme has none
            return QIcon::fromTheme("folder", QApplication::style()->standardIcon(QStyle::SP_DirIcon));
        case IdRole:
            return -1;
        case FolderIdRole:
            return n->id;
        default:
            return QVariant();
        }
    }

    const Snippet &snippet = n->snippet;
    switch (role) {
    case IdRole:
        return snippet.id;
    case FolderIdRole:
        return -1;
    case TitleRole:
    case Qt::DisplayRole:
        return snippet.title;
    case Qt::ToolTipRole:
        // Where it is, when the list doesn't show folders
        return !showsFolders() && !snippet.folderPath.isEmpty() ? "In " + snippet.folderPath : QVariant();
    case ContentRole:
        return snippet.content;
    case CreatedRole:
        return snippet.created;
    case ModifiedRole:
        return snippet.modified;
    case PreviewRole: {
        auto it = m_previews.constFind(snippet.id);
        if (it != m_previews.constEnd()) return *it;
        QString text = snippet.isRich() ? RichText::toPlainText(snippet.content) : snippet.content;
        QString preview;
        for (const QString &line : text.split('\n')) {
            if (!line.trimmed().isEmpty()) {
                preview = line.trimmed().left(200);
                break;
            }
        }
        m_previews.insert(snippet.id, preview);
        return preview;
    }
    default:
        return QVariant();
    }
}

Qt::ItemFlags SnippetModel::flags(const QModelIndex &index) const
{
    bool dragAndDrop = showsFolders();
    if (!index.isValid()) {
        // Dropping on empty space moves things to the top level
        return dragAndDrop ? Qt::ItemIsDropEnabled : Qt::NoItemFlags;
    }
    Qt::ItemFlags flags = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (dragAndDrop) {
        flags |= Qt::ItemIsDragEnabled;
        if (isFolder(index)) flags |= Qt::ItemIsDropEnabled;
    }
    if (!isFolder(index)) flags |= Qt::ItemNeverHasChildren;
    return flags;
}

QHash<int, QByteArray> SnippetModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[IdRole] = "id";
    roles[TitleRole] = "title";
    roles[ContentRole] = "content";
    roles[CreatedRole] = "created";
    roles[ModifiedRole] = "modified";
    roles[FolderIdRole] = "folderId";
    return roles;
}

QStringList SnippetModel::mimeTypes() const
{
    return {ItemsMimeType};
}

QMimeData *SnippetModel::mimeData(const QModelIndexList &indexes) const
{
    // A list of (is folder, id) pairs
    QByteArray encoded;
    QDataStream stream(&encoded, QIODevice::WriteOnly);
    for (const QModelIndex &index : indexes) {
        if (const Node *n = node(index)) {
            stream << n->folder << n->id;
        }
    }
    auto *data = new QMimeData;
    data->setData(ItemsMimeType, encoded);
    return data;
}

Qt::DropActions SnippetModel::supportedDropActions() const
{
    return Qt::MoveAction;
}

bool SnippetModel::canDropMimeData(const QMimeData *data, Qt::DropAction, int, int, const QModelIndex &parent) const
{
    return showsFolders() && data->hasFormat(ItemsMimeType) && (!parent.isValid() || isFolder(parent));
}

bool SnippetModel::dropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column,
                                const QModelIndex &parent)
{
    if (!canDropMimeData(data, action, row, column, parent)) return false;
    int targetFolder = parent.isValid() ? node(parent)->id : -1;

    QByteArray encoded = data->data(ItemsMimeType);
    QDataStream stream(&encoded, QIODevice::ReadOnly);
    QString error;
    while (!stream.atEnd()) {
        bool folder = false;
        int id = -1;
        stream >> folder >> id;
        QString itemError;
        bool moved = folder ? Database::instance().moveFolder(id, targetFolder, &itemError)
                            : Database::instance().moveSnippetToFolder(id, targetFolder, &itemError);
        if (!moved && error.isEmpty()) error = itemError;
    }
    // The view is still handling the drop, so refresh afterwards. (The view
    // also tries to remove the dragged rows itself; removeRows() isn't
    // implemented, so that does nothing.)
    QTimer::singleShot(0, this, [this, error]() { emit itemsMoved(error); });
    return true;
}

void SnippetModel::search(const QString &searchTerm)
{
    m_filter.search = searchTerm;
    refresh();
}

void SnippetModel::setTagFilter(const QString &tag, bool untagged)
{
    m_filter.tag = tag;
    m_filter.untagged = untagged;
    refresh();
}

void SnippetModel::setShowTrash(bool trash)
{
    m_filter.trash = trash;
    refresh();
}

void SnippetModel::setFlat(bool flat)
{
    m_flat = flat;
    refresh();
}

void SnippetModel::setSortOrder(Database::SortOrder order)
{
    m_sortOrder = order;
    refresh();
}

Database::SortOrder SnippetModel::sortOrder() const
{
    return m_sortOrder;
}

QModelIndex SnippetModel::indexForSnippet(int id) const
{
    if (id == -1) return QModelIndex();
    for (int i = 0; i < m_nodes.size(); ++i) {
        if (!m_nodes[i].folder && m_nodes[i].id == id) return indexForNode(i);
    }
    return QModelIndex();
}

QModelIndex SnippetModel::indexForFolder(int id) const
{
    if (id == -1) return QModelIndex();
    for (int i = 0; i < m_nodes.size(); ++i) {
        if (m_nodes[i].folder && m_nodes[i].id == id) return indexForNode(i);
    }
    return QModelIndex();
}

QList<Snippet> SnippetModel::snippets() const
{
    QList<Snippet> result;
    for (const Node &n : m_nodes) {
        if (!n.folder) result.append(n.snippet);
    }
    return result;
}

int SnippetModel::neighbourOf(int snippetId) const
{
    QModelIndex index = indexForSnippet(snippetId);
    if (!index.isValid()) return -1;
    const Node *n = node(index);
    const QList<int> &siblings = n->parent == -1 ? m_topLevel : m_nodes[n->parent].children;
    for (int i = n->row + 1; i < siblings.size(); ++i) {
        if (!m_nodes[siblings[i]].folder) return m_nodes[siblings[i]].id;
    }
    for (int i = n->row - 1; i >= 0; --i) {
        if (!m_nodes[siblings[i]].folder) return m_nodes[siblings[i]].id;
    }
    return -1;
}
