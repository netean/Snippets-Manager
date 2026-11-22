#include "snippetmodel.h"
#include "richtext.h"

SnippetModel::SnippetModel(QObject *parent)
    : QAbstractListModel(parent)
{
    refresh();
}

int SnippetModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent)
    return m_snippets.size();
}

QVariant SnippetModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_snippets.size())
        return QVariant();

    const Snippet &snippet = m_snippets.at(index.row());

    switch (role) {
    case IdRole:
        return snippet.id;
    case TitleRole:
    case Qt::DisplayRole:
        return snippet.title;
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

QHash<int, QByteArray> SnippetModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[IdRole] = "id";
    roles[TitleRole] = "title";
    roles[ContentRole] = "content";
    roles[CreatedRole] = "created";
    roles[ModifiedRole] = "modified";
    return roles;
}

void SnippetModel::refresh()
{
    beginResetModel();
    m_previews.clear();
    m_snippets = Database::instance().snippets(m_filter, m_sortOrder);
    endResetModel();
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

void SnippetModel::setSortOrder(Database::SortOrder order)
{
    m_sortOrder = order;
    refresh();
}

Database::SortOrder SnippetModel::sortOrder() const
{
    return m_sortOrder;
}

Snippet SnippetModel::getSnippet(int row) const
{
    if (row >= 0 && row < m_snippets.size()) {
        return m_snippets.at(row);
    }
    return Snippet();
}

int SnippetModel::rowForId(int id) const
{
    for (int row = 0; row < m_snippets.size(); ++row) {
        if (m_snippets.at(row).id == id) {
            return row;
        }
    }
    return -1;
}
