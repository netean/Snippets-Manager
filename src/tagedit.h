#ifndef TAGEDIT_H
#define TAGEDIT_H

#include <QLineEdit>
#include <QStringList>

class QCompleter;
class QStringListModel;

// A line edit for comma-separated tags that completes each tag from the
// tags already in use
class TagEdit : public QLineEdit
{
    Q_OBJECT

public:
    explicit TagEdit(QWidget *parent = nullptr);

    QStringList tags() const;
    void setTags(const QStringList &tags);
    void setKnownTags(const QStringList &tags);

protected:
    void keyPressEvent(QKeyEvent *event) override;

private:
    void updateCompletions();
    void insertCompletion(const QString &tag);
    QString currentTag() const;

    QCompleter *m_completer;
    QStringListModel *m_suggestions;
    QStringList m_knownTags;
};

#endif // TAGEDIT_H
