#ifndef CODEHIGHLIGHTER_H
#define CODEHIGHLIGHTER_H

#include <QSyntaxHighlighter>
#include <QTextCharFormat>

struct LanguageSpec;

// A small built-in syntax highlighter for common languages, used when the
// app is built without KDE's KSyntaxHighlighting (e.g. the AppImage, Windows
// and macOS builds). It colours keywords, types, strings, comments, numbers
// and a few language-specific things, in colours that suit a light or dark
// colour scheme.
class CodeHighlighter : public QSyntaxHighlighter
{
    Q_OBJECT

public:
    explicit CodeHighlighter(QObject *parent = nullptr);

    // Whether there are rules for this language (names as in the Type list)
    static bool supports(const QString &language);

    // Sets the language (empty for none) and colours to suit the palette
    void setLanguage(const QString &language, const QPalette &palette);

protected:
    void highlightBlock(const QString &text) override;

private:
    void highlightCode(const QString &text);
    void highlightMarkup(const QString &text);
    void highlightMarkdown(const QString &text);
    void highlightDiff(const QString &text);
    void highlightIni(const QString &text);
    void highlightYaml(const QString &text);
    void applyPattern(const QString &text, const QString &pattern, const QTextCharFormat &format, int group = 0);
    int scanString(const QString &text, int start, const QString &quote);

    const LanguageSpec *m_spec = nullptr;
    QTextCharFormat m_keyword, m_type, m_string, m_comment, m_number, m_function,
        m_preprocessor, m_variable, m_tag, m_attribute, m_heading, m_added, m_removed, m_key;
};

#endif // CODEHIGHLIGHTER_H
