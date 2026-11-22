#include "richtext.h"
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextList>

namespace RichText {

static const auto Dialect = QTextDocument::MarkdownDialectGitHub;

QString toHtml(const QString &markdown)
{
    QTextDocument document;
    document.setMarkdown(markdown, Dialect);
    return document.toHtml();
}

QString toPlainText(const QString &markdown)
{
    QTextDocument document;
    document.setMarkdown(markdown, Dialect);
    return document.toPlainText();
}

QString fromPlainText(const QString &text)
{
    QTextDocument document;
    document.setPlainText(text);
    return toMarkdown(&document);
}

// ---------------------------------------------------------------------------
// Writing Markdown
//
// Qt's own Markdown writer doesn't escape special characters before Qt 6.5,
// so "2*3*4" would come back as italics. This writer covers what the editor
// can produce (paragraphs, headings, lists, bold, italic, strikethrough,
// code and links) and escapes everything else, on every Qt version.
// ---------------------------------------------------------------------------

// Escapes characters that Markdown would otherwise treat as formatting
static QString escapeText(const QString &text)
{
    QString result;
    result.reserve(text.size() + 8);
    for (QChar c : text) {
        if (QStringLiteral("\\`*_[]<>~|&").contains(c)) {
            result += '\\';
        }
        result += c;
    }
    return result;
}

QString escape(const QString &text)
{
    QStringList lines = text.split('\n');
    for (QString &line : lines) {
        line = escapeText(line);
    }
    return lines.join('\n');
}

// Escapes what would start a heading, list or quote at the start of a line
static QString escapeLineStart(const QString &line)
{
    static const QRegularExpression orderedListStart("^(\\d+)([.)])");
    QRegularExpressionMatch match = orderedListStart.match(line);
    if (match.hasMatch()) {
        return match.captured(1) + "\\" + line.mid(match.captured(1).size());
    }
    if (!line.isEmpty() && QStringLiteral("#-+=").contains(line.at(0))) {
        return "\\" + line;
    }
    return line;
}

// Wraps code in enough backticks that backticks inside it don't end it
static QString codeSpan(const QString &code)
{
    int longestRun = 0, run = 0;
    for (QChar c : code) {
        run = c == '`' ? run + 1 : 0;
        longestRun = qMax(longestRun, run);
    }
    QString fence(longestRun + 1, '`');
    bool pad = code.startsWith('`') || code.endsWith('`');
    return fence + (pad ? " " : "") + code + (pad ? " " : "") + fence;
}

struct Span {
    QString text;
    bool bold = false;
    bool italic = false;
    bool strike = false;
    bool code = false;
    QString link;

    bool sameStyle(const Span &other) const
    {
        return bold == other.bold && italic == other.italic && strike == other.strike
               && code == other.code && link == other.link;
    }
};

static QString writeSpan(const Span &span)
{
    // Emphasis markers must touch the text, so keep surrounding spaces outside them
    QString text = span.text;
    int lead = 0;
    while (lead < text.size() && text.at(lead).isSpace()) ++lead;
    int trail = 0;
    while (trail < text.size() - lead && text.at(text.size() - 1 - trail).isSpace()) ++trail;
    QString before = text.left(lead);
    QString after = text.right(trail);
    QString core = text.mid(lead, text.size() - lead - trail);
    if (core.isEmpty()) {
        return escapeText(text);
    }

    QString written = span.code ? codeSpan(core) : escapeText(core);
    if (span.strike) written = "~~" + written + "~~";
    if (span.italic) written = "*" + written + "*";
    if (span.bold) written = "**" + written + "**";
    if (!span.link.isEmpty()) {
        QString url = span.link;
        url.replace('(', "%28").replace(')', "%29").replace(' ', "%20");
        written = "[" + written + "](" + url + ")";
    }
    return before + written + after;
}

static QString writeBlockText(const QTextBlock &block)
{
    // Collect the block's text as spans, merging neighbours with the same style
    QList<Span> spans;
    for (auto it = block.begin(); !it.atEnd(); ++it) {
        QTextFragment fragment = it.fragment();
        if (!fragment.isValid()) continue;
        QTextCharFormat format = fragment.charFormat();
        Span span;
        // Images (e.g. pasted from a web page) appear as an object replacement
        // character; rich snippets don't support them, so leave them out
        span.text = fragment.text().remove(QChar::ObjectReplacementCharacter);
        if (span.text.isEmpty()) continue;
        span.bold = format.fontWeight() >= QFont::Bold;
        span.italic = format.fontItalic();
        span.strike = format.fontStrikeOut();
        span.code = format.fontFixedPitch();
        span.link = format.isAnchor() ? format.anchorHref() : QString();
        if (!spans.isEmpty() && spans.last().sameStyle(span)) {
            spans.last().text += span.text;
        } else {
            spans.append(span);
        }
    }

    // Line breaks inside a block (Shift+Enter) become Markdown hard breaks
    QString markdown;
    for (const Span &span : spans) {
        QStringList lines = span.text.split(QChar::LineSeparator);
        for (int i = 0; i < lines.size(); ++i) {
            if (i > 0) markdown += "\\\n";
            Span part = span;
            part.text = lines.at(i);
            markdown += writeSpan(part);
        }
    }

    // Escape anything that would be read as block structure at a line start
    QStringList lines = markdown.split('\n');
    for (QString &line : lines) {
        line = escapeLineStart(line);
    }
    return lines.join('\n');
}

QString toMarkdown(const QTextDocument *document)
{
    QStringList blocks;
    // The list the previous block belonged to, if any
    QTextList *previousList = nullptr;

    for (QTextBlock block = document->begin(); block.isValid(); block = block.next()) {
        QString text = writeBlockText(block);
        QTextList *list = block.textList();

        if (list) {
            int level = qMax(0, list->format().indent() - 1);
            QTextListFormat::Style style = list->format().style();
            bool numbered = style == QTextListFormat::ListDecimal || style == QTextListFormat::ListLowerAlpha
                            || style == QTextListFormat::ListUpperAlpha || style == QTextListFormat::ListLowerRoman
                            || style == QTextListFormat::ListUpperRoman;
            QString marker = numbered ? QString::number(list->itemNumber(block) + 1) + ". " : QString("- ");
            QString indent(level * 4, ' ');
            // Continuation lines of the item are indented to line up with its text
            text.replace("\n", "\n" + indent + QString(marker.size(), ' '));
            QString item = indent + marker + text;
            // Items of the same list (or a nested one) go on consecutive lines;
            // a different list at the same level needs a blank line to stay separate
            bool continuesList = previousList && !blocks.isEmpty()
                                 && (previousList == list || previousList->format().indent() != list->format().indent());
            if (continuesList) {
                blocks.last() += "\n" + item;
            } else {
                blocks.append(item);
            }
            previousList = list;
            continue;
        }

        previousList = nullptr;
        if (text.trimmed().isEmpty()) {
            continue;  // Markdown can't represent empty paragraphs
        }
        int heading = block.blockFormat().headingLevel();
        if (heading > 0) {
            text = QString(qMin(heading, 6), '#') + " " + text;
        }
        blocks.append(text);
    }
    return blocks.join("\n\n");
}

} // namespace RichText
