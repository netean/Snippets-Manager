#ifndef RICHTEXT_H
#define RICHTEXT_H

#include <QString>

class QTextDocument;

// Rich text snippets are stored as (GitHub-flavoured) Markdown, so they stay
// readable in plain text, in exports and in older versions of the app.
namespace RichText {

QString toHtml(const QString &markdown);
QString toPlainText(const QString &markdown);
// Markdown for text that should appear exactly as written, with no formatting
QString fromPlainText(const QString &text);
// Writes a document as Markdown, escaping anything that isn't formatting.
// The same on every Qt version (Qt's own writer doesn't escape before 6.5).
QString toMarkdown(const QTextDocument *document);
// Escapes text so Markdown shows it as written
QString escape(const QString &text);

} // namespace RichText

#endif // RICHTEXT_H
