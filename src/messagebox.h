#ifndef MESSAGEBOX_H
#define MESSAGEBOX_H

#include <QMessageBox>

// QMessageBox's static helpers guess whether text is HTML, so a snippet title
// or file path containing markup would be rendered as formatting. These always
// show the text exactly as written.
namespace MessageBox {

inline QMessageBox::StandardButton show(QWidget *parent, QMessageBox::Icon icon, const QString &title,
                                        const QString &text, QMessageBox::StandardButtons buttons,
                                        QMessageBox::StandardButton defaultButton = QMessageBox::NoButton)
{
    QMessageBox box(icon, title, text, buttons, parent);
    box.setTextFormat(Qt::PlainText);
    if (defaultButton != QMessageBox::NoButton) {
        box.setDefaultButton(defaultButton);
    }
    return static_cast<QMessageBox::StandardButton>(box.exec());
}

inline void warning(QWidget *parent, const QString &title, const QString &text)
{
    show(parent, QMessageBox::Warning, title, text, QMessageBox::Ok);
}

inline void information(QWidget *parent, const QString &title, const QString &text)
{
    show(parent, QMessageBox::Information, title, text, QMessageBox::Ok);
}

inline void critical(QWidget *parent, const QString &title, const QString &text)
{
    show(parent, QMessageBox::Critical, title, text, QMessageBox::Ok);
}

inline bool question(QWidget *parent, const QString &title, const QString &text,
                     QMessageBox::StandardButton defaultButton = QMessageBox::No)
{
    return show(parent, QMessageBox::Question, title, text, QMessageBox::Yes | QMessageBox::No, defaultButton)
           == QMessageBox::Yes;
}

} // namespace MessageBox

#endif // MESSAGEBOX_H
