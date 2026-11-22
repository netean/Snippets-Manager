#include "clipboard.h"
#include "database.h"
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
    bool richText = QSettings().value(SettingsKeys::RichText, true).toBool();
    if (snippet.isRich() && richText) {
        copy(RichText::toPlainText(snippet.content), RichText::toHtml(snippet.content));
    } else {
        copy(snippet.content);
    }
}

} // namespace Clipboard
