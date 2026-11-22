#ifndef PLACEHOLDERS_H
#define PLACEHOLDERS_H

#include <QDateTime>
#include <QString>
#include <functional>

// Placeholders like {{date}} that are filled in when a snippet is copied
namespace Placeholders {

struct Context {
    QDateTime now;
    QString clipboard;
    std::function<QString()> uuid;
};

// The current time, clipboard text and a random UUID generator
Context currentContext();

// Replaces {{date}}, {{time}}, {{datetime}} (each optionally with a Qt date
// format, e.g. {{date:dd/MM/yyyy}}), {{clipboard}} and {{uuid}}. Unknown
// names are left alone, and \{{...}} gives the text without the backslash.
// With markdown set, filled-in values are escaped so they appear as written.
QString expand(const QString &text, const Context &context, bool markdown = false);

// Help text describing the placeholders, for the settings
QString helpText();

} // namespace Placeholders

#endif // PLACEHOLDERS_H
