#ifndef CLIPBOARD_H
#define CLIPBOARD_H

#include <QString>

struct Snippet;

namespace Clipboard {

// Copies text, with an optional HTML version for apps that accept formatting.
// Honours the "keep out of clipboard history" setting.
void copy(const QString &plainText, const QString &html = QString());

// Copies a snippet's content: formatted and plain for rich text snippets
// (unless rich text is turned off), plain text otherwise
void copySnippet(const Snippet &snippet);

} // namespace Clipboard

#endif // CLIPBOARD_H
