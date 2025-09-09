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
    m_snippets = Database::instance().getAllSnippets();
    endResetModel();
}

void SnippetModel::search(const QString &searchTerm)
{
    beginResetModel();
    if (searchTerm.isEmpty()) {
        m_snippets = Database::instance().getAllSnippets();
    } else {
        m_snippets = Database::instance().searchSnippets(searchTerm);
    }
    endResetModel();
}

Snippet SnippetModel::getSnippet(int row) const
{
    if (row >= 0 && row < m_snippets.size()) {
        return m_snippets.at(row);
    }
    return Snippet();
}