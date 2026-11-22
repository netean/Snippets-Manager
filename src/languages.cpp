#include "languages.h"

namespace Languages {

QStringList all()
{
    // Names match KSyntaxHighlighting's definitions, so snippets highlight
    // correctly when it's available
    return {"Bash", "C", "C#", "C++", "CMake", "CSS", "Diff", "Dockerfile", "Fish", "Go",
            "HTML", "INI Files", "Java", "JavaScript", "JSON", "Kotlin", "Lua", "Makefile",
            "Markdown", "Perl", "PHP", "PowerShell", "Python", "Ruby", "Rust", "SQL",
            "Swift", "TOML", "TypeScript", "XML", "YAML", "Zsh"};
}

} // namespace Languages
