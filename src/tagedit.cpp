#include "tagedit.h"
#include "database.h"
#include <QAbstractItemView>
#include <QCompleter>
#include <QKeyEvent>
#include <QStringListModel>

TagEdit::TagEdit(QWidget *parent)
    : QLineEdit(parent)
    , m_suggestions(new QStringListModel(this))
{
    setPlaceholderText("Tags, separated by commas");
    setClearButtonEnabled(true);

    m_completer = new QCompleter(m_suggestions, this);
    m_completer->setCaseSensitivity(Qt::CaseInsensitive);
    m_completer->setFilterMode(Qt::MatchStartsWith);
    m_completer->setWidget(this);
    connect(m_completer, qOverload<const QString &>(&QCompleter::activated), this, &TagEdit::insertCompletion);
    connect(this, &QLineEdit::textEdited, this, &TagEdit::updateCompletions);
}

QStringList TagEdit::tags() const
{
    return Database::normalizeTags(text().split(','));
}

void TagEdit::setTags(const QStringList &tags)
{
    setText(tags.join(", "));
}

void TagEdit::setKnownTags(const QStringList &tags)
{
    m_knownTags = tags;
}

QString TagEdit::currentTag() const
{
    // The tag being typed is the text after the last comma before the cursor
    QString beforeCursor = text().left(cursorPosition());
    return beforeCursor.mid(beforeCursor.lastIndexOf(',') + 1).trimmed();
}

void TagEdit::updateCompletions()
{
    QString prefix = currentTag();
    if (prefix.isEmpty()) {
        m_completer->popup()->hide();
        return;
    }
    // Don't suggest tags that are already on the snippet
    QStringList existing = tags();
    QStringList suggestions;
    for (const QString &tag : m_knownTags) {
        if (!existing.contains(tag, Qt::CaseInsensitive) || tag.compare(prefix, Qt::CaseInsensitive) == 0) {
            suggestions << tag;
        }
    }
    m_suggestions->setStringList(suggestions);
    m_completer->setCompletionPrefix(prefix);
    if (m_completer->completionCount() > 0) {
        m_completer->complete();
    } else {
        m_completer->popup()->hide();
    }
}

void TagEdit::insertCompletion(const QString &tag)
{
    // Replace the tag being typed with the chosen one
    QString beforeCursor = text().left(cursorPosition());
    QString afterCursor = text().mid(cursorPosition());
    int start = beforeCursor.lastIndexOf(',') + 1;
    QString replaced = beforeCursor.left(start) + (start > 0 ? " " : "") + tag + ", ";
    // Drop the rest of the partly typed tag after the cursor, keep later tags
    int comma = afterCursor.indexOf(',');
    QString rest = comma >= 0 ? afterCursor.mid(comma + 1).trimmed() : QString();
    setText(replaced + rest);
    setCursorPosition(replaced.size());
}

void TagEdit::keyPressEvent(QKeyEvent *event)
{
    // Let the completer handle choosing a suggestion
    if (m_completer->popup()->isVisible()) {
        switch (event->key()) {
        case Qt::Key_Enter:
        case Qt::Key_Return:
        case Qt::Key_Escape:
        case Qt::Key_Tab:
        case Qt::Key_Backtab:
            event->ignore();
            return;
        default:
            break;
        }
    }
    QLineEdit::keyPressEvent(event);
}
