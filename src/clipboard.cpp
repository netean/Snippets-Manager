#include "clipboard.h"
#include "database.h"
#include "placeholders.h"
#include "richtext.h"
#include "settingsdialog.h"
#include <QApplication>
#include <QClipboard>
#include <QMimeData>
#include <QSettings>

namespace Clipboard {

void copy(const QString &plainText, const QString &html)
{
    auto *mimeData = new QMimeData;
    mimeData->setText(plainText);
    if (!html.isEmpty()) {
        mimeData->setHtml(html);
    }
    if (QSettings().value(SettingsKeys::HideFromHistory, false).toBool()) {
        // Tells clipboard managers that follow this convention (such as KDE's
        // Klipper) not to store the text in their history
        mimeData->setData("x-kde-passwordManagerHint", "secret");
    }
    QApplication::clipboard()->setMimeData(mimeData);
}

void copySnippet(const Snippet &snippet)
{
    QSettings settings;
    bool richText = snippet.isRich() && settings.value(SettingsKeys::RichText, true).toBool();
    QString content = snippet.content;
    if (settings.value(SettingsKeys::Placeholders, true).toBool()) {
        content = Placeholders::expand(content, Placeholders::currentContext(), richText);
    }
    if (richText) {
        copy(RichText::toPlainText(content), RichText::toHtml(content));
    } else {
        copy(content);
    }
}

} // namespace Clipboard
