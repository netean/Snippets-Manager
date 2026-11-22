#include "languages.h"
#include <QCollator>
#include <algorithm>

#ifdef HAVE_SYNTAX_HIGHLIGHTING
#include <KSyntaxHighlighting/Definition>
#include <KSyntaxHighlighting/Repository>
#endif

namespace Languages {

#ifdef HAVE_SYNTAX_HIGHLIGHTING
KSyntaxHighlighting::Repository &repository()
{
    static KSyntaxHighlighting::Repository repository;
    return repository;
}
#endif

bool highlightingAvailable()
{
    // Always: KSyntaxHighlighting when the build has it, else the built-in highlighter
    return true;
}

bool usesKSyntaxHighlighting()
{
#ifdef HAVE_SYNTAX_HIGHLIGHTING
    return true;
#else
    return false;
#endif
}

QStringList all()
{
    static QStringList languages;
    if (!languages.isEmpty()) {
        return languages;
    }

#ifdef HAVE_SYNTAX_HIGHLIGHTING
    for (const KSyntaxHighlighting::Definition &definition : repository().definitions()) {
        if (!definition.isHidden()) {
            languages << definition.name();
        }
    }
#endif
    if (languages.isEmpty()) {
        // Names match KSyntaxHighlighting's definitions, so snippets highlight
        // correctly in builds that have it
        languages = {"Bash", "C", "C#", "C++", "CMake", "CSS", "Diff", "Dockerfile", "Fish", "Go",
                     "HTML", "INI Files", "Java", "JavaScript", "JSON", "Kotlin", "Lua", "Makefile",
                     "Markdown", "Perl", "PHP", "PowerShell", "Python", "Ruby", "Rust", "SQL",
                     "Swift", "TOML", "TypeScript", "XML", "YAML", "Zsh"};
    }

    QCollator collator;
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(languages.begin(), languages.end(), collator);
    return languages;
}

} // namespace Languages
