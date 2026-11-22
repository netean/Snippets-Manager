#include "placeholders.h"
#include "richtext.h"
#include <QApplication>
#include <QClipboard>
#include <QRegularExpression>
#include <QUuid>

namespace Placeholders {

Context currentContext()
{
    Context context;
    context.now = QDateTime::currentDateTime();
    context.clipboard = QApplication::clipboard()->text();
    context.uuid = []() { return QUuid::createUuid().toString(QUuid::WithoutBraces); };
    return context;
}

QString expand(const QString &text, const Context &context, bool markdown)
{
    // An optional backslash (to escape it), then {{name}} or {{name:format}}
    static const QRegularExpression placeholder(R"((\\?)\{\{\s*([A-Za-z]+)(?::([^}]*))?\s*\}\})");

    QString result;
    qsizetype last = 0;
    QRegularExpressionMatchIterator it = placeholder.globalMatch(text);
    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        result += text.mid(last, match.capturedStart() - last);
        last = match.capturedEnd();

        QString whole = match.captured(0);
        if (!match.captured(1).isEmpty()) {
            result += whole.mid(1);  // escaped: keep it, without the backslash
            continue;
        }

        QString name = match.captured(2).toLower();
        QString format = match.captured(3).trimmed();
        QString value;
        bool known = true;
        if (name == "date") {
            value = context.now.toString(format.isEmpty() ? "yyyy-MM-dd" : format);
        } else if (name == "time") {
            value = context.now.toString(format.isEmpty() ? "HH:mm" : format);
        } else if (name == "datetime") {
            value = context.now.toString(format.isEmpty() ? "yyyy-MM-dd HH:mm" : format);
        } else if (name == "clipboard") {
            value = context.clipboard;
        } else if (name == "uuid") {
            value = context.uuid ? context.uuid() : QString();
        } else {
            known = false;
        }

        if (!known) {
            result += whole;
        } else {
            result += markdown ? RichText::escape(value) : value;
        }
    }
    result += text.mid(last);
    return result;
}

QString helpText()
{
    return "{{date}}, {{time}}, {{datetime}}: the current date and time. Add a format to "
           "change how they look, e.g. {{date:dd/MM/yyyy}} or {{time:h:mm ap}}.\n"
           "{{clipboard}}: what was on the clipboard before copying.\n"
           "{{uuid}}: a new random ID.\n"
           "To copy one as written, put a backslash before it: \\{{date}}.";
}

} // namespace Placeholders
