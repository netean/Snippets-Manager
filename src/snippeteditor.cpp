#include "snippeteditor.h"
#include "tagedit.h"
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QTextEdit>
#include <QVBoxLayout>

SnippetEditor::SnippetEditor(QWidget *parent)
    : QWidget(parent)
{
    m_titleEdit = new QLineEdit;
    m_titleEdit->setObjectName("titleEdit");
    m_tagEdit = new TagEdit;
    m_tagEdit->setObjectName("tagEdit");

    QFormLayout *fields = new QFormLayout;
    fields->setContentsMargins(0, 0, 0, 0);
    fields->addRow("Title:", m_titleEdit);
    fields->addRow("Tags:", m_tagEdit);

    m_contentEdit = new QTextEdit;
    m_contentEdit->setObjectName("contentEdit");
    m_contentEdit->setAcceptRichText(false);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addLayout(fields);
    layout->addWidget(new QLabel("Content:"));
    layout->addWidget(m_contentEdit, 1);

    connect(m_titleEdit, &QLineEdit::textChanged, this, &SnippetEditor::markChanged);
    connect(m_tagEdit, &QLineEdit::textChanged, this, &SnippetEditor::markChanged);
    connect(m_contentEdit, &QTextEdit::textChanged, this, &SnippetEditor::markChanged);
    connect(m_titleEdit, &QLineEdit::returnPressed, this, &SnippetEditor::returnPressed);
    connect(m_tagEdit, &QLineEdit::returnPressed, this, &SnippetEditor::returnPressed);

    setSnippet(Snippet());
}

void SnippetEditor::setSnippet(const Snippet &snippet)
{
    m_loading = true;
    m_snippet = snippet;
    m_titleEdit->setText(snippet.title);
    m_tagEdit->setTags(snippet.tags);
    m_contentEdit->setPlainText(snippet.content);
    m_loading = false;
    m_modified = false;

    bool hasSnippet = snippet.isValid();
    m_titleEdit->setEnabled(hasSnippet);
    m_tagEdit->setEnabled(hasSnippet);
    m_contentEdit->setEnabled(hasSnippet);
    setContentEditable(false);
}

Snippet SnippetEditor::snippet() const
{
    Snippet snippet = m_snippet;
    snippet.title = m_titleEdit->text().trimmed();
    if (snippet.title.isEmpty()) {
        snippet.title = "Untitled";
    }
    snippet.tags = m_tagEdit->tags();
    snippet.content = m_contentEdit->toPlainText();
    return snippet;
}

void SnippetEditor::setSaved(const Snippet &saved)
{
    m_loading = true;
    m_snippet = saved;
    if (m_titleEdit->text() != saved.title) {
        m_titleEdit->setText(saved.title);
    }
    if (m_tagEdit->tags() != saved.tags) {
        m_tagEdit->setTags(saved.tags);
    }
    m_loading = false;
    m_modified = false;
}

void SnippetEditor::setContentEditable(bool editable)
{
    m_contentEdit->setReadOnly(!editable);
    // Keep the text selectable in view mode too
    m_contentEdit->setTextInteractionFlags(editable ? Qt::TextEditorInteraction
                                                    : Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    m_contentEdit->viewport()->setCursor(editable ? Qt::IBeamCursor : Qt::PointingHandCursor);
}

bool SnippetEditor::isContentEditable() const
{
    return !m_contentEdit->isReadOnly();
}

void SnippetEditor::setKnownTags(const QStringList &tags)
{
    m_tagEdit->setKnownTags(tags);
}

QString SnippetEditor::plainText() const
{
    return m_contentEdit->toPlainText();
}

QString SnippetEditor::selectedText() const
{
    QString selected = m_contentEdit->textCursor().selectedText();
    // QTextCursor uses the Unicode paragraph separator for line breaks
    selected.replace(QChar::ParagraphSeparator, '\n');
    return selected;
}

QWidget *SnippetEditor::contentViewport() const
{
    return m_contentEdit->viewport();
}

void SnippetEditor::focusTitle()
{
    m_titleEdit->selectAll();
    m_titleEdit->setFocus();
}

void SnippetEditor::focusContent()
{
    m_contentEdit->setFocus();
}

void SnippetEditor::markChanged()
{
    if (m_loading) return;
    m_modified = true;
    emit changed();
}
