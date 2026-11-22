#ifndef QUICKSEARCH_H
#define QUICKSEARCH_H

#include <QWidget>

class QLabel;
class QLineEdit;
class QListView;
class SnippetModel;

// A small search window, opened from the tray or a global shortcut: type to
// find a snippet, press Enter to copy it
class QuickSearch : public QWidget
{
    Q_OBJECT

public:
    explicit QuickSearch(QWidget *parent = nullptr);

    // Shows the window with an empty search. On Wayland, the activation token
    // from the shortcut or launcher lets it take the keyboard focus.
    void popup(const QString &activationToken = QString());

signals:
    // Ctrl+Enter: show this snippet in the main window
    void openRequested(int id);
    void copied(const QString &title);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    bool event(QEvent *event) override;

private:
    void copyCurrent();
    int currentId() const;

    QLineEdit *m_search;
    QListView *m_list;
    QLabel *m_hint;
    SnippetModel *m_model;
};

#endif // QUICKSEARCH_H
