#ifndef SNIPPETEDITOR_H
#define SNIPPETEDITOR_H

#include <QWidget>
#include <QTextDocumentFragment>
#include "database.h"

class QAction;
class QComboBox;
class QLineEdit;
class QTextCharFormat;
class QTextEdit;
class QToolBar;
class TagEdit;

// The right-hand side of the main window: a snippet's title, tags, type and
// content, with a small formatting toolbar for rich text snippets
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

    // The title and tags can always be changed; the type and content only while editing
    void setContentEditable(bool editable);
    bool isContentEditable() const;

    void setKnownTags(const QStringList &tags);
    // Re-reads the editor settings (rich text on or off)
    void applySettings();

    // The content as shown, as plain text
    QString plainText() const;
    // The selected part of the content
    QTextDocumentFragment selection() const;
    // Whether the content is shown with formatting
    bool showsFormatting() const { return m_formatted; }

    QWidget *contentViewport() const;
    void focusTitle();
    void focusContent();

signals:
    // Emitted whenever the user changes the title, tags, type or content
    void changed();
    // Emitted when Enter is pressed in the title or tags field
    void returnPressed();

private:
    void markChanged();
    void populateTypes();
    void selectType(const Snippet &snippet);
    void showContent(const QString &content);
    void onTypeChanged();
    void updateToolbar();
    void updateFormatActions();
    void mergeFormat(const QTextCharFormat &format);
    void toggleList(int style);
    void clearFormatting();
    bool richTextEnabled() const;
    // Whether the current type should be displayed with formatting
    bool shouldFormat() const;

    Snippet m_snippet;
    // The type and language currently chosen (may differ from m_snippet until saved)
    Snippet::Format m_format = Snippet::PlainText;
    QString m_language;
    bool m_modified = false;
    bool m_loading = false;
    // Whether the content is currently displayed with formatting (rather than
    // as plain text or Markdown source)
    bool m_formatted = false;

    QLineEdit *m_titleEdit;
    TagEdit *m_tagEdit;
    QComboBox *m_typeCombo;
    QToolBar *m_formatBar;
    QTextEdit *m_contentEdit;

    QAction *m_boldAction;
    QAction *m_italicAction;
    QAction *m_strikeAction;
    QAction *m_codeAction;
    QAction *m_bulletAction;
    QAction *m_numberAction;
};

#endif // SNIPPETEDITOR_H
