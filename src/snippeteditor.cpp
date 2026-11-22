#include "snippeteditor.h"
#include "codehighlighter.h"
#include "languages.h"
#include "messagebox.h"
#include "richtext.h"
#include "settingsdialog.h"
#include "tagedit.h"
#include <QAction>
#include <QActionGroup>
#include <QInputDialog>
#include <QKeyEvent>
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
#else
    m_builtinHighlighter = new CodeHighlighter(this);
#endif

    // Formatting toolbar, shown while editing plain or rich text. Using it on
    // a plain text snippet turns it into rich text.
    m_formatBar = new QToolBar;
    m_formatBar->setObjectName("formatBar");
    m_formatBar->setIconSize(QSize(16, 16));
    auto makeAction = [this](const QString &icon, const QString &text, const QString &toolTip,
                             const QKeySequence &shortcut = QKeySequence(), bool checkable = true) {
        // The full name shows in the Format menu; the toolbar falls back to the
        // short label when the icon theme lacks the icon
        QAction *action = new QAction(QIcon::fromTheme(icon), toolTip, this);
        action->setIconText(text);
        action->setToolTip(shortcut.isEmpty() ? toolTip
                                              : QString("%1 (%2)").arg(toolTip, shortcut.toString(QKeySequence::NativeText)));
        action->setStatusTip(toolTip);
        action->setCheckable(checkable);
        if (!shortcut.isEmpty()) {
            action->setShortcut(shortcut);
            action->setShortcutContext(Qt::WidgetShortcut);
            m_contentEdit->addAction(action);
        }
        return action;
    };

    // Paragraph style: normal text or a heading
    m_styleCombo = new QComboBox;
    m_styleCombo->setObjectName("styleCombo");
    m_styleCombo->setToolTip("Paragraph style");
    m_styleCombo->setFocusPolicy(Qt::NoFocus);
    m_styleGroup = new QActionGroup(this);
    const QStringList styles = {"Normal Text", "Heading 1", "Heading 2", "Heading 3"};
    for (int level = 0; level < styles.size(); ++level) {
        m_styleCombo->addItem(styles[level], level);
        QAction *action = m_styleGroup->addAction(styles[level]);
        action->setCheckable(true);
        action->setData(level);
        action->setShortcut(QKeySequence(QString("Ctrl+%1").arg(level)));
        action->setShortcutContext(Qt::WidgetShortcut);
        m_contentEdit->addAction(action);
        connect(action, &QAction::triggered, this, [this, level]() { applyHeading(level); });
    }
    connect(m_styleCombo, &QComboBox::activated, this, [this](int index) { applyHeading(index); });

    m_boldAction = makeAction("format-text-bold", "B", "Bold", QKeySequence::Bold);
    m_italicAction = makeAction("format-text-italic", "I", "Italic", QKeySequence::Italic);
    m_underlineAction = makeAction("format-text-underline", "U", "Underline", QKeySequence::Underline);
    m_strikeAction = makeAction("format-text-strikethrough", "S", "Strikethrough");
    m_codeAction = makeAction("format-text-code", "<>", "Code");
    m_linkAction = makeAction("insert-link", "Link", "Link", QKeySequence("Ctrl+K"), false);
    m_bulletAction = makeAction("format-list-unordered", "• List", "Bulleted list");
    m_numberAction = makeAction("format-list-ordered", "1. List", "Numbered list");
    m_checklistAction = makeAction("checkbox", "☐ List", "Checklist");
    m_outdentAction = makeAction("format-indent-less", "Outdent", "Outdent list item", QKeySequence(), false);
    m_indentAction = makeAction("format-indent-more", "Indent", "Indent list item", QKeySequence(), false);
    m_clearAction = makeAction("edit-clear-all", "Clear", "Clear formatting", QKeySequence(), false);

    m_formatBar->addWidget(m_styleCombo);
    m_formatBar->addSeparator();
    m_formatBar->addActions({m_boldAction, m_italicAction, m_underlineAction, m_strikeAction, m_codeAction, m_linkAction});
    m_formatBar->addSeparator();
    m_formatBar->addActions({m_bulletAction, m_numberAction, m_checklistAction, m_outdentAction, m_indentAction});
    m_formatBar->addSeparator();
    m_formatBar->addAction(m_clearAction);

    auto charFormatAction = [this](QAction *action, std::function<void(QTextCharFormat &, bool)> apply) {
        connect(action, &QAction::triggered, this, [this, apply](bool checked) {
            if (!prepareFormatting()) return;
            QTextCharFormat format;
            apply(format, checked);
            mergeFormat(format);
        });
    };
    charFormatAction(m_boldAction, [](QTextCharFormat &f, bool on) { f.setFontWeight(on ? QFont::Bold : QFont::Normal); });
    charFormatAction(m_italicAction, [](QTextCharFormat &f, bool on) { f.setFontItalic(on); });
    charFormatAction(m_underlineAction, [](QTextCharFormat &f, bool on) { f.setFontUnderline(on); });
    charFormatAction(m_strikeAction, [](QTextCharFormat &f, bool on) { f.setFontStrikeOut(on); });
    charFormatAction(m_codeAction, [this](QTextCharFormat &f, bool on) {
        // Markdown writes fixed-pitch text as `code`
        f.setFontFixedPitch(on);
        f.setFontFamilies(on ? QStringList{QFontDatabase::systemFont(QFontDatabase::FixedFont).family()}
                             : QStringList{font().family()});
    });
    connect(m_linkAction, &QAction::triggered, this, &SnippetEditor::editLink);
    connect(m_bulletAction, &QAction::triggered, this, [this]() { toggleList(QTextListFormat::ListDisc); });
    connect(m_numberAction, &QAction::triggered, this, [this]() { toggleList(QTextListFormat::ListDecimal); });
    connect(m_checklistAction, &QAction::triggered, this, &SnippetEditor::toggleChecklist);
    connect(m_indentAction, &QAction::triggered, this, [this]() { changeListIndent(1); });
    connect(m_outdentAction, &QAction::triggered, this, [this]() { changeListIndent(-1); });
    connect(m_clearAction, &QAction::triggered, this, &SnippetEditor::clearFormatting);
    // Tab and Shift+Tab indent and outdent list items
    m_contentEdit->installEventFilter(this);
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

SnippetEditor::~SnippetEditor()
{
    // Removing a highlighter clears its colours, which signals a text change;
    // stop listening first, while this object is still whole
    disconnect(m_contentEdit, nullptr, this, nullptr);
    delete m_builtinHighlighter;
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
    // Re-colouring isn't an edit by the user
    bool wasLoading = m_loading;
    m_loading = true;
    auto restore = qScopeGuard([this, wasLoading]() { m_loading = wasLoading; });
    bool enabled = QSettings().value(SettingsKeys::SyntaxHighlighting, true).toBool()
                   && !m_formatted && !m_language.isEmpty();

#ifndef HAVE_SYNTAX_HIGHLIGHTING
    // The built-in highlighter, attached only while it's needed
    if (!enabled || !CodeHighlighter::supports(m_language)) {
        if (m_builtinHighlighter->document()) m_builtinHighlighter->setDocument(nullptr);
        return;
    }
    if (!m_builtinHighlighter->document()) m_builtinHighlighter->setDocument(m_contentEdit->document());
    m_builtinHighlighter->setLanguage(m_language, palette());
#else
    KSyntaxHighlighting::Definition definition;
    if (enabled) {
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
    m_typeCombo->setEnabled(editable && m_snippet.isValid() && !m_locked);
    updateToolbar();
}

bool SnippetEditor::isContentEditable() const
{
    return !m_contentEdit->isReadOnly();
}

bool SnippetEditor::canFormat() const
{
    // Rich text, or plain text that can become rich text (not code)
    return isContentEditable() && !m_locked && m_snippet.isValid() && richTextEnabled()
           && (m_format == Snippet::RichText || m_language.isEmpty());
}

void SnippetEditor::updateToolbar()
{
    bool available = canFormat();
    m_formatBar->setVisible(available);
    for (QAction *action : formatActions()) {
        if (action) action->setEnabled(available);
    }
    updateFormatActions();
}

QList<QAction *> SnippetEditor::formatActions() const
{
    // nullptr marks a separator
    QList<QAction *> actions = m_styleGroup->actions();
    actions << nullptr << m_boldAction << m_italicAction << m_underlineAction << m_strikeAction << m_codeAction
            << m_linkAction << nullptr << m_bulletAction << m_numberAction << m_checklistAction << m_outdentAction
            << m_indentAction << nullptr << m_clearAction;
    return actions;
}

void SnippetEditor::updateFormatActions()
{
    QTextCursor cursor = m_contentEdit->textCursor();
    QTextList *list = m_formatted ? cursor.currentList() : nullptr;
    QTextCharFormat format = m_formatted ? m_contentEdit->currentCharFormat() : QTextCharFormat();
    QTextBlockFormat block = m_formatted ? cursor.blockFormat() : QTextBlockFormat();
    int heading = qBound(0, block.headingLevel(), 3);
    bool marked = block.marker() != QTextBlockFormat::MarkerType::NoMarker;

    m_boldAction->setChecked(heading == 0 && format.fontWeight() >= QFont::Bold);
    m_italicAction->setChecked(format.fontItalic());
    m_underlineAction->setChecked(format.fontUnderline() && !format.isAnchor());
    m_strikeAction->setChecked(format.fontStrikeOut());
    m_codeAction->setChecked(format.fontFixedPitch());
    m_bulletAction->setChecked(list && !marked && list->format().style() == QTextListFormat::ListDisc);
    m_numberAction->setChecked(list && list->format().style() == QTextListFormat::ListDecimal);
    m_checklistAction->setChecked(list && marked);
    m_indentAction->setEnabled(m_formatBar->isVisible() && list);
    m_outdentAction->setEnabled(m_formatBar->isVisible() && list);
    m_styleCombo->setCurrentIndex(heading);
    m_styleGroup->actions().at(heading)->setChecked(true);
}

bool SnippetEditor::prepareFormatting()
{
    if (m_formatted) return true;
    if (!canFormat()) return false;

    // Plain text becomes rich text, exactly as written. Keep the selection, so
    // the formatting applies to what the user selected.
    QTextCursor before = m_contentEdit->textCursor();
    int anchor = before.anchor();
    int position = before.position();
    QString text = m_contentEdit->toPlainText();
    m_typeCombo->setCurrentIndex(m_typeCombo->findData("rich"));
    if (!m_formatted) return false;
    if (m_contentEdit->toPlainText() == text) {
        QTextCursor cursor = m_contentEdit->textCursor();
        cursor.setPosition(anchor);
        cursor.setPosition(position, QTextCursor::KeepAnchor);
        m_contentEdit->setTextCursor(cursor);
    }
    return true;
}

void SnippetEditor::applyHeading(int level)
{
    if (!prepareFormatting()) return;
    QTextCursor cursor = m_contentEdit->textCursor();
    cursor.beginEditBlock();
    QTextBlockFormat blockFormat;
    blockFormat.setHeadingLevel(level);
    cursor.mergeBlockFormat(blockFormat);

    // Headings are bigger and bold, like Qt shows Markdown headings
    QTextDocument *document = m_contentEdit->document();
    QTextBlock first = document->findBlock(cursor.selectionStart());
    QTextBlock last = document->findBlock(cursor.selectionEnd());
    QTextCursor whole(document);
    whole.setPosition(first.position());
    whole.setPosition(last.position() + last.length() - 1, QTextCursor::KeepAnchor);
    QTextCharFormat charFormat;
    charFormat.setProperty(QTextFormat::FontSizeAdjustment, level > 0 ? 4 - level : 0);
    charFormat.setFontWeight(level > 0 ? QFont::Bold : QFont::Normal);
    whole.mergeCharFormat(charFormat);
    cursor.endEditBlock();
    m_contentEdit->mergeCurrentCharFormat(charFormat);
    m_contentEdit->setFocus();
    updateFormatActions();
}

void SnippetEditor::toggleChecklist()
{
    if (!prepareFormatting()) return;
    QTextCursor cursor = m_contentEdit->textCursor();
    bool turnOff = cursor.blockFormat().marker() != QTextBlockFormat::MarkerType::NoMarker;
    cursor.beginEditBlock();
    if (!turnOff && !cursor.currentList()) {
        cursor.createList(QTextListFormat::ListDisc);
    }
    QTextDocument *document = m_contentEdit->document();
    QTextBlock end = document->findBlock(cursor.selectionEnd()).next();
    for (QTextBlock block = document->findBlock(cursor.selectionStart()); block.isValid() && block != end;
         block = block.next()) {
        QTextCursor blockCursor(block);
        QTextBlockFormat format = block.blockFormat();
        if (turnOff) {
            format.setMarker(QTextBlockFormat::MarkerType::NoMarker);
            if (QTextList *list = block.textList()) list->remove(block);
            format.setIndent(0);
        } else {
            format.setMarker(QTextBlockFormat::MarkerType::Unchecked);
        }
        blockCursor.setBlockFormat(format);
    }
    cursor.endEditBlock();
    m_contentEdit->setFocus();
    updateFormatActions();
}

void SnippetEditor::changeListIndent(int delta)
{
    if (!m_formatted) return;
    QTextCursor cursor = m_contentEdit->textCursor();
    QTextList *list = cursor.currentList();
    if (!list) return;
    cursor.beginEditBlock();
    QTextListFormat format = list->format();
    int indent = format.indent() + delta;
    if (indent < 1) {
        // Outdenting the top level turns the item back into a paragraph
        QTextBlock block = cursor.block();
        list->remove(block);
        QTextBlockFormat blockFormat = block.blockFormat();
        blockFormat.setIndent(0);
        blockFormat.setMarker(QTextBlockFormat::MarkerType::NoMarker);
        cursor.setBlockFormat(blockFormat);
    } else {
        format.setIndent(indent);
        cursor.createList(format);
    }
    cursor.endEditBlock();
    updateFormatActions();
}

void SnippetEditor::editLink()
{
    if (!prepareFormatting()) return;
    QTextCursor cursor = m_contentEdit->textCursor();
    QString current = cursor.charFormat().anchorHref();
    if (!cursor.hasSelection() && !current.isEmpty()) {
        // Edit the whole link the cursor is in
        cursor.select(QTextCursor::WordUnderCursor);
    }
    bool ok = false;
    QString url = QInputDialog::getText(this, "Link", "Link address (leave empty to remove the link):",
                                        QLineEdit::Normal, current.isEmpty() ? QString("https://") : current, &ok)
                      .trimmed();
    if (!ok) return;

    QTextCharFormat format;
    if (url.isEmpty() || url == "https://") {
        format.setAnchor(false);
        format.setAnchorHref(QString());
        format.setFontUnderline(false);
        format.setForeground(palette().text());
    } else {
        format.setAnchor(true);
        format.setAnchorHref(url);
        format.setFontUnderline(true);
        format.setForeground(palette().link());
    }
    if (cursor.hasSelection()) {
        cursor.mergeCharFormat(format);
    } else if (!url.isEmpty() && url != "https://") {
        // Nothing selected: insert the address itself as the link text
        cursor.insertText(url, format);
        cursor.setCharFormat(QTextCharFormat());
    }
    m_contentEdit->setFocus();
    updateFormatActions();
}

bool SnippetEditor::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_contentEdit && event->type() == QEvent::KeyPress && m_formatted && isContentEditable()) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        if ((keyEvent->key() == Qt::Key_Tab || keyEvent->key() == Qt::Key_Backtab)
            && m_contentEdit->textCursor().currentList()) {
            bool outdent = keyEvent->key() == Qt::Key_Backtab || (keyEvent->modifiers() & Qt::ShiftModifier);
            changeListIndent(outdent ? -1 : 1);
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
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
    if (!prepareFormatting()) return;
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
    if (!m_formatted) return;
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
        // Also removes heading levels and checklist markers
        QTextCursor blockCursor(block);
        blockCursor.setBlockFormat(QTextBlockFormat());
    }
    cursor.endEditBlock();
    m_contentEdit->setCurrentCharFormat(QTextCharFormat());
    m_contentEdit->setFocus();
    updateFormatActions();
}

void SnippetEditor::setLocked(bool locked)
{
    m_locked = locked;
    bool enabled = m_snippet.isValid() && !locked;
    m_titleEdit->setReadOnly(locked);
    m_tagEdit->setReadOnly(locked);
    m_titleEdit->setEnabled(m_snippet.isValid());
    m_tagEdit->setEnabled(m_snippet.isValid());
    if (locked) {
        setContentEditable(false);
    }
    m_typeCombo->setEnabled(enabled && isContentEditable());
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
