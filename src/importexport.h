#ifndef IMPORTEXPORT_H
#define IMPORTEXPORT_H

#include <QByteArray>
#include <QList>
#include <QString>
#include "database.h"

// Converting snippets to and from files
namespace ImportExport {

enum class Format { Json, Markdown, Csv, Unknown };

// Picks the format from a file name's extension
Format formatForPath(const QString &path);

// JSON keeps everything and is the best format for backups
QByteArray toJson(const QList<Snippet> &snippets);
QList<Snippet> fromJson(const QByteArray &data, QString *errorMessage = nullptr);

// Markdown: one "## Title" section per snippet, with its content in a
// fenced block and its details in an HTML comment
QString toMarkdown(const QList<Snippet> &snippets);
QList<Snippet> fromMarkdown(const QString &text, QString *errorMessage = nullptr);

// CSV with a header row. Import recognises common column names (title/name,
// content/body/text, tags/category, ...) and comma, semicolon or tab separators.
QString toCsv(const QList<Snippet> &snippets);
QList<Snippet> fromCsv(const QString &text, QString *errorMessage = nullptr);

// Reads any supported file, by its extension
QList<Snippet> readFile(const QString &path, QString *errorMessage = nullptr);
// Writes snippets in the format given by the extension, private to the user
bool writeFile(const QString &path, const QList<Snippet> &snippets, QString *errorMessage = nullptr);

} // namespace ImportExport

#endif // IMPORTEXPORT_H
