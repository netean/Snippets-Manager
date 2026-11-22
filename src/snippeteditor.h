#ifndef SNIPPETEDITOR_H
#define SNIPPETEDITOR_H

#include <QWidget>
#include "database.h"

class QLineEdit;
class QTextEdit;
class TagEdit;

// The right-hand side of the main window: a snippet's title, tags and content
class SnippetEditor : public QWidget
{
    Q_OBJECT

public:
    explicit SnippetEditor(QWidget *parent = nullptr);

    // Shows a snippet (or nothing, for an invalid one) and clears the modified flag
    void setSnippet(const Snippet &snippet);
    // The loaded snippet with the user's changes applied
    Snippet snippet() const;
    // Records that the snippet was saved as given (e.g. with tidied tags),
    // without disturbing the content being edited
    void setSaved(const Snippet &saved);

    bool isModified() const { return m_modified; }
    void setModified(bool modified) { m_modified = modified; }

    // The title and tags can always be changed; the content only while editing
    void setContentEditable(bool editable);
    bool isContentEditable() const;

    void setKnownTags(const QStringList &tags);

    // The content as plain text, and the part of it that's selected
    QString plainText() const;
    QString selectedText() const;

    QWidget *contentViewport() const;
    void focusTitle();
    void focusContent();

signals:
    // Emitted whenever the user changes the title, tags or content
    void changed();
    // Emitted when Enter is pressed in the title or tags field
    void returnPressed();

private:
    void markChanged();

    Snippet m_snippet;
    bool m_modified = false;
    bool m_loading = false;

    QLineEdit *m_titleEdit;
    TagEdit *m_tagEdit;
    QTextEdit *m_contentEdit;
};

#endif // SNIPPETEDITOR_H
