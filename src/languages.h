#ifndef LANGUAGES_H
#define LANGUAGES_H

#include <QStringList>

class QTextDocument;
class QPalette;

namespace Languages {

// The languages a code snippet can be marked as, sorted by name
QStringList all();

// Whether this build can highlight code (it needs KSyntaxHighlighting)
bool highlightingAvailable();

} // namespace Languages

#ifdef HAVE_SYNTAX_HIGHLIGHTING
namespace KSyntaxHighlighting { class Repository; }
namespace Languages {
// Shared, since loading the language definitions takes a moment
KSyntaxHighlighting::Repository &repository();
}
#endif

#endif // LANGUAGES_H
