#include "snippeteditor.h"
#include "languages.h"
#include "messagebox.h"
#include "richtext.h"
#include "settingsdialog.h"
#include "tagedit.h"
#include <QAction>
#include <QComboBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSettings>
#include <QTextCursor>
#include <QTextEdit>
#include <QTextList>
#include <QToolBar>
#include <QVBoxLayout>
#include <QScopeGuard>

#ifdef HAVE_SYNTAX_HIGHLIGHTING
#include <KSyntaxHighlighting/Definition>
#include <KSyntaxHighlighting/Repository>
#include <KSyntaxHighlighting/SyntaxHighlighter>
#include <KSyntaxHighlighting/Theme>
#endif

// Type combo entries: "plain", "rich" or "code:<language>"
static QString typeKey(Snippet::Format format, const QString &language)
{
    if (format == Snippet::RichText) return "rich";
    return language.isEmpty() ? "plain" : "code:" + language;
}

SnippetEditor::SnippetEditor(QWidget *parent)
    : QWidget(parent)
{
    m_titleEdit = new QLineEdit;
    m_titleEdit->setObjectName("titleEdit");
    m_tagEdit = new TagEdit;
    m_tagEdit->setObjectName("tagEdit");
    m_typeCombo = new QComboBox;
    m_typeCombo->setObjectName("typeCombo");
    m_typeCombo->setToolTip("Plain text, formatted rich text, or code in a particular language");
    m_typeCombo->setMaxVisibleItems(20);

    QFormLayout *fields = new QFormLayout;
    fields->setContentsMargins(0, 0, 0, 0);
    fields->addRow("Title:", m_titleEdit);
    fields->addRow("Tags:", m_tagEdit);
    QHBoxLayout *typeLayout = new QHBoxLayout;
    typeLayout->addWidget(m_typeCombo);
    typeLayout->addStretch();
    fields->addRow("Type:", typeLayout);

    m_contentEdit = new QTextEdit;
    m_contentEdit->setObjectName("contentEdit");
    m_contentEdit->setAcceptRichText(false);
#ifdef HAVE_SYNTAX_HIGHLIGHTING
    m_highlighter = new KSyntaxHighlighting::SyntaxHighlighter(m_contentEdit->document());
#endif

    // Formatting toolbar, shown while editing rich text
    m_formatBar = new QToolBar;
    m_formatBar->setObjectName("formatBar");
    m_formatBar->setIconSize(QSize(16, 16));
    auto addFormatAction = [this](const QString &icon, const QString &text, const QString &toolTip,
                                  const QKeySequence &shortcut = QKeySequence()) {
        // Falls back to the short text label when the icon theme lacks the icon
        QAction *action = m_formatBar->addAction(QIcon::fromTheme(icon), text);
        action->setToolTip(shortcut.isEmpty() ? toolTip
                                              : QString("%1 (%2)").arg(toolTip, shortcut.toString(QKeySequence::NativeText)));
        action->setCheckable(true);
        if (!shortcut.isEmpty()) {
            action->setShortcut(shortcut);
            action->setShortcutContext(Qt::WidgetShortcut);
            m_contentEdit->addAction(action);
        }
        return action;
    };
    m_boldAction = addFormatAction("format-text-bold", "B", "Bold", QKeySequence::Bold);
    m_italicAction = addFormatAction("format-text-italic", "I", "Italic", QKeySequence::Italic);
    m_strikeAction = addFormatAction("format-text-strikethrough", "S", "Strikethrough");
    m_codeAction = addFormatAction("format-text-code", "<>", "Code");
    m_formatBar->addSeparator();
    m_bulletAction = addFormatAction("format-list-unordered", "• List", "Bulleted list");
    m_numberAction = addFormatAction("format-list-ordered", "1. List", "Numbered list");
    m_formatBar->addSeparator();
    QAction *clearAction = m_formatBar->addAction(QIcon::fromTheme("edit-clear-all"), "Clear");
    clearAction->setToolTip("Clear formatting");

    connect(m_boldAction, &QAction::triggered, this, [this](bool checked) {
        QTextCharFormat format;
        format.setFontWeight(checked ? QFont::Bold : QFont::Normal);
        mergeFormat(format);
    });
    connect(m_italicAction, &QAction::triggered, this, [this](bool checked) {
        QTextCharFormat format;
        format.setFontItalic(checked);
        mergeFormat(format);
    });
    connect(m_strikeAction, &QAction::triggered, this, [this](bool checked) {
        QTextCharFormat format;
        format.setFontStrikeOut(checked);
        mergeFormat(format);
    });
    connect(m_codeAction, &QAction::triggered, this, [this](bool checked) {
        // Markdown writes fixed-pitch text as `code`
        QTextCharFormat format;
        format.setFontFixedPitch(checked);
        format.setFontFamilies(checked ? QStringList{QFontDatabase::systemFont(QFontDatabase::FixedFont).family()}
                                       : QStringList{font().family()});
        mergeFormat(format);
    });
    connect(m_bulletAction, &QAction::triggered, this, [this]() { toggleList(QTextListFormat::ListDisc); });
    connect(m_numberAction, &QAction::triggered, this, [this]() { toggleList(QTextListFormat::ListDecimal); });
    connect(clearAction, &QAction::triggered, this, &SnippetEditor::clearFormatting);
    connect(m_contentEdit, &QTextEdit::currentCharFormatChanged, this, &SnippetEditor::updateFormatActions);
    connect(m_contentEdit, &QTextEdit::cursorPositionChanged, this, &SnippetEditor::updateFormatActions);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addLayout(fields);
    layout->addWidget(new QLabel("Content:"));
    layout->addWidget(m_formatBar);
    layout->addWidget(m_contentEdit, 1);

    connect(m_titleEdit, &QLineEdit::textChanged, this, &SnippetEditor::markChanged);
    connect(m_tagEdit, &QLineEdit::textChanged, this, &SnippetEditor::markChanged);
    connect(m_contentEdit, &QTextEdit::textChanged, this, &SnippetEditor::markChanged);
    connect(m_typeCombo, &QComboBox::currentIndexChanged, this, &SnippetEditor::onTypeChanged);
    connect(m_titleEdit, &QLineEdit::returnPressed, this, &SnippetEditor::returnPressed);
    connect(m_tagEdit, &QLineEdit::returnPressed, this, &SnippetEditor::returnPressed);

    setSnippet(Snippet());
}

bool SnippetEditor::richTextEnabled() const
{
    return QSettings().value(SettingsKeys::RichText, true).toBool();
}

bool SnippetEditor::shouldFormat() const
{
    return m_format == Snippet::RichText && richTextEnabled();
}

void SnippetEditor::populateTypes()
{
    const QSignalBlocker blocker(m_typeCombo);
    m_typeCombo->clear();
    m_typeCombo->addItem("Plain text", "plain");
    // With rich text turned off, only offer it for snippets that already use it
    if (richTextEnabled() || m_format == Snippet::RichText) {
        m_typeCombo->addItem(richTextEnabled() ? "Rich text" : "Rich text (shown as Markdown)", "rich");
    }
    m_typeCombo->insertSeparator(m_typeCombo->count());
    QStringList languages = Languages::all();
    if (!m_language.isEmpty() && !languages.contains(m_language)) {
        languages.append(m_language);  // e.g. from an import
    }
    for (const QString &language : languages) {
        m_typeCombo->addItem(language, "code:" + language);
    }
}

void SnippetEditor::selectType(const Snippet &snippet)
{
    const QSignalBlocker blocker(m_typeCombo);
    int index = m_typeCombo->findData(typeKey(snippet.format, snippet.language));
    m_typeCombo->setCurrentIndex(index >= 0 ? index : 0);
}

void SnippetEditor::showContent(const QString &content)
{
    bool wasLoading = m_loading;
    m_loading = true;
    m_formatted = shouldFormat();
    if (m_formatted) {
        m_contentEdit->setAcceptRichText(true);
        m_contentEdit->setMarkdown(content);
    } else {
        m_contentEdit->setAcceptRichText(false);
        m_contentEdit->setPlainText(content);
    }
    // Code reads best in a fixed-width font
    m_contentEdit->document()->setDefaultFont(m_language.isEmpty() || m_format == Snippet::RichText
                                                  ? font()
                                                  : QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_loading = wasLoading;
    updateHighlighting();
    updateToolbar();
}

void SnippetEditor::updateHighlighting()
{
#ifdef HAVE_SYNTAX_HIGHLIGHTING
    // Re-colouring isn't an edit by the user
    bool wasLoading = m_loading;
    m_loading = true;
    auto restore = qScopeGuard([this, wasLoading]() { m_loading = wasLoading; });
    bool enabled = QSettings().value(SettingsKeys::SyntaxHighlighting, true).toBool();
    KSyntaxHighlighting::Definition definition;
    if (enabled && !m_formatted && !m_language.isEmpty()) {
        definition = Languages::repository().definitionForName(m_language);
    }
    if (!definition.isValid()) {
        // Detach completely: even without a language, the highlighter would
        // colour the text with its theme's default colours
        if (m_highlighter->document()) {
            m_highlighter->setDocument(nullptr);
        }
        return;
    }
    if (!m_highlighter->document()) {
        m_highlighter->setDocument(m_contentEdit->document());
    }
    // Match the colours to a light or dark colour scheme
    m_highlighter->setTheme(Languages::repository().themeForPalette(palette()));
    m_highlighter->setDefinition(definition);
    m_highlighter->rehighlight();
#endif
}

void SnippetEditor::changeEvent(QEvent *event)
{
    // Switching between light and dark mode changes the highlighting colours
    if (event->type() == QEvent::PaletteChange) {
        updateHighlighting();
    }
    QWidget::changeEvent(event);
}

void SnippetEditor::setSnippet(const Snippet &snippet)
{
    m_loading = true;
    m_snippet = snippet;
    m_format = snippet.format;
    m_language = snippet.isRich() ? QString() : snippet.language;
    m_titleEdit->setText(snippet.title);
    m_tagEdit->setTags(snippet.tags);
    populateTypes();
    selectType(snippet);
    showContent(snippet.content);
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
    snippet.format = m_format;
    snippet.language = m_language;
    if (m_formatted) {
        snippet.content = RichText::toMarkdown(m_contentEdit->document());
    } else {
        // Plain text, or the Markdown itself when rich text is turned off
        snippet.content = m_contentEdit->toPlainText();
    }
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

void SnippetEditor::onTypeChanged()
{
    QString key = m_typeCombo->currentData().toString();
    Snippet::Format newFormat = key == "rich" ? Snippet::RichText : Snippet::PlainText;
    QString newLanguage = key.startsWith("code:") ? key.mid(5) : QString();

    // The content as plain text, with any Markdown rendered
    QString plain = m_format == Snippet::RichText && !m_formatted
                        ? RichText::toPlainText(m_contentEdit->toPlainText())
                        : m_contentEdit->toPlainText();

    if (m_format == Snippet::RichText && newFormat != Snippet::RichText) {
        QString markdown = m_formatted ? snippet().content : m_contentEdit->toPlainText();
        bool hasFormatting = markdown != RichText::fromPlainText(plain);
        if (hasFormatting && !MessageBox::question(this, "Change Type",
                                                   "Changing to this type removes the snippet's formatting. Continue?")) {
            Snippet current;
            current.format = m_format;
            current.language = m_language;
            selectType(current);
            return;
        }
    }

    bool formatChanged = newFormat != m_format;
    m_format = newFormat;
    m_language = newLanguage;
    if (formatChanged) {
        showContent(newFormat == Snippet::RichText ? RichText::fromPlainText(plain) : plain);
        populateTypes();
        Snippet current;
        current.format = m_format;
        current.language = m_language;
        selectType(current);
    } else {
        showContent(m_contentEdit->toPlainText());
    }
    markChanged();
}

void SnippetEditor::applySettings()
{
    // Re-show the content, keeping any unsaved changes. The content is read
    // before the setting takes effect, while it's still shown the old way.
    bool modified = m_modified;
    bool editable = isContentEditable();
    Snippet current = snippet();
    m_loading = true;
    populateTypes();
    selectType(current);
    showContent(current.content);
    m_loading = false;
    m_modified = modified;
    setContentEditable(editable);
}

void SnippetEditor::setContentEditable(bool editable)
{
    m_contentEdit->setReadOnly(!editable);
    // Keep the text selectable in view mode too
    m_contentEdit->setTextInteractionFlags(editable ? Qt::TextEditorInteraction
                                                    : Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    m_contentEdit->viewport()->setCursor(editable ? Qt::IBeamCursor : Qt::PointingHandCursor);
    m_typeCombo->setEnabled(editable && m_snippet.isValid());
    updateToolbar();
}

bool SnippetEditor::isContentEditable() const
{
    return !m_contentEdit->isReadOnly();
}

void SnippetEditor::updateToolbar()
{
    m_formatBar->setVisible(m_formatted && isContentEditable());
    updateFormatActions();
}

void SnippetEditor::updateFormatActions()
{
    if (!m_formatBar->isVisible()) return;
    QTextCharFormat format = m_contentEdit->currentCharFormat();
    m_boldAction->setChecked(format.fontWeight() >= QFont::Bold);
    m_italicAction->setChecked(format.fontItalic());
    m_strikeAction->setChecked(format.fontStrikeOut());
    m_codeAction->setChecked(format.fontFixedPitch());
    QTextList *list = m_contentEdit->textCursor().currentList();
    m_bulletAction->setChecked(list && list->format().style() == QTextListFormat::ListDisc);
    m_numberAction->setChecked(list && list->format().style() == QTextListFormat::ListDecimal);
}

void SnippetEditor::mergeFormat(const QTextCharFormat &format)
{
    QTextCursor cursor = m_contentEdit->textCursor();
    if (cursor.hasSelection()) {
        cursor.mergeCharFormat(format);
    }
    // Also applies to whatever is typed next
    m_contentEdit->mergeCurrentCharFormat(format);
    m_contentEdit->setFocus();
}

void SnippetEditor::toggleList(int style)
{
    QTextCursor cursor = m_contentEdit->textCursor();
    QTextList *list = cursor.currentList();
    cursor.beginEditBlock();
    if (list && list->format().style() == style) {
        // Already this kind of list: turn the selected lines back into paragraphs
        QTextBlock block = m_contentEdit->document()->findBlock(cursor.selectionStart());
        QTextBlock end = m_contentEdit->document()->findBlock(cursor.selectionEnd()).next();
        for (; block.isValid() && block != end; block = block.next()) {
            if (QTextList *blockList = block.textList()) {
                blockList->remove(block);
                QTextCursor blockCursor(block);
                QTextBlockFormat blockFormat = block.blockFormat();
                blockFormat.setIndent(0);
                blockCursor.setBlockFormat(blockFormat);
            }
        }
    } else if (list) {
        QTextListFormat listFormat = list->format();
        listFormat.setStyle(static_cast<QTextListFormat::Style>(style));
        list->setFormat(listFormat);
    } else {
        cursor.createList(static_cast<QTextListFormat::Style>(style));
    }
    cursor.endEditBlock();
    m_contentEdit->setFocus();
    updateFormatActions();
}

void SnippetEditor::clearFormatting()
{
    // Clears the selection, or everything if nothing is selected
    QTextCursor cursor = m_contentEdit->textCursor();
    if (!cursor.hasSelection()) {
        cursor.select(QTextCursor::Document);
    }
    cursor.beginEditBlock();
    cursor.setCharFormat(QTextCharFormat());
    QTextBlock block = m_contentEdit->document()->findBlock(cursor.selectionStart());
    QTextBlock end = m_contentEdit->document()->findBlock(cursor.selectionEnd()).next();
    for (; block.isValid() && block != end; block = block.next()) {
        if (QTextList *list = block.textList()) {
            list->remove(block);
        }
        QTextCursor blockCursor(block);
        blockCursor.setBlockFormat(QTextBlockFormat());
    }
    cursor.endEditBlock();
    m_contentEdit->setCurrentCharFormat(QTextCharFormat());
    m_contentEdit->setFocus();
    updateFormatActions();
}

void SnippetEditor::setKnownTags(const QStringList &tags)
{
    m_tagEdit->setKnownTags(tags);
}

QString SnippetEditor::plainText() const
{
    return m_contentEdit->toPlainText();
}

QTextDocumentFragment SnippetEditor::selection() const
{
    return m_contentEdit->textCursor().selection();
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
