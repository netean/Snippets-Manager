#include "snippetmodel.h"

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
    if (m_searchTerm.isEmpty()) {
        m_snippets = Database::instance().getAllSnippets(m_sortOrder);
    } else {
        m_snippets = Database::instance().searchSnippets(m_searchTerm, m_sortOrder);
    }
    endResetModel();
}

void SnippetModel::search(const QString &searchTerm)
{
    m_searchTerm = searchTerm;
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
