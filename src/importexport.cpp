#include "importexport.h"
#include "languages.h"
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStringConverter>

namespace ImportExport {

Format formatForPath(const QString &path)
{
    QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == "json") return Format::Json;
    if (suffix == "md" || suffix == "markdown") return Format::Markdown;
    if (suffix == "csv" || suffix == "tsv" || suffix == "txt") return Format::Csv;
    return Format::Unknown;
}

// ---------------------------------------------------------------------------
// Shared helpers
// ---------------------------------------------------------------------------

static QString typeName(const Snippet &snippet)
{
    if (snippet.isRich()) return "rich";
    return snippet.language.isEmpty() ? "plain" : "code";
}

static QString isoTime(const QDateTime &time)
{
    return time.isValid() ? time.toUTC().toString(Qt::ISODate) : QString();
}

static QDateTime parseTime(const QString &text)
{
    QDateTime time = QDateTime::fromString(text.trimmed(), Qt::ISODate);
    return time.isValid() ? time.toLocalTime() : QDateTime();
}

// Matches a language name or common alias (e.g. "py", "sh") to a known language
static QString matchLanguage(const QString &name)
{
    QString key = name.trimmed().toLower();
    if (key.isEmpty() || key == "text" || key == "plain" || key == "plaintext" || key == "txt") {
        return QString();
    }
    static const QHash<QString, QString> aliases = {
        {"sh", "Bash"}, {"shell", "Bash"}, {"console", "Bash"}, {"zsh", "Zsh"}, {"fish", "Fish"},
        {"py", "Python"}, {"python3", "Python"}, {"js", "JavaScript"}, {"ts", "TypeScript"},
        {"cpp", "C++"}, {"cxx", "C++"}, {"cs", "C#"}, {"csharp", "C#"}, {"rb", "Ruby"}, {"rs", "Rust"},
        {"yml", "YAML"}, {"ps1", "PowerShell"}, {"pwsh", "PowerShell"}, {"golang", "Go"},
        {"md", "Markdown"}, {"ini", "INI Files"}, {"docker", "Dockerfile"}, {"make", "Makefile"},
        {"patch", "Diff"}, {"htm", "HTML"}, {"kt", "Kotlin"}, {"pl", "Perl"},
    };
    QString candidate = aliases.value(key, name.trimmed());
    for (const QString &language : Languages::all()) {
        if (language.compare(candidate, Qt::CaseInsensitive) == 0) {
            return language;
        }
    }
    return QString();
}

// Sets format and language from a type ("plain", "rich", "code" or a language name)
static void applyType(Snippet &snippet, const QString &type, const QString &language)
{
    QString key = type.trimmed().toLower();
    if (key == "rich" || key == "richtext" || key == "rich text" || key == "markdown-rich") {
        snippet.format = Snippet::RichText;
        snippet.language.clear();
        return;
    }
    snippet.format = Snippet::PlainText;
    snippet.language = matchLanguage(language.isEmpty() && key != "plain" && key != "code" ? type : language);
}

static QStringList splitTags(const QString &text)
{
    static const QRegularExpression separators("[,;]");
    return Database::normalizeTags(text.split(separators));
}

// ---------------------------------------------------------------------------
// JSON
// ---------------------------------------------------------------------------

QByteArray toJson(const QList<Snippet> &snippets)
{
    QJsonArray array;
    for (const Snippet &snippet : snippets) {
        QJsonObject object;
        object["title"] = snippet.title;
        object["content"] = snippet.content;
        object["type"] = typeName(snippet);
        if (!snippet.language.isEmpty()) object["language"] = snippet.language;
        object["tags"] = QJsonArray::fromStringList(snippet.tags);
        if (!snippet.folderPath.isEmpty()) object["folder"] = snippet.folderPath;
        if (snippet.created.isValid()) object["created"] = isoTime(snippet.created);
        if (snippet.modified.isValid()) object["modified"] = isoTime(snippet.modified);
        array.append(object);
    }
    QJsonObject root;
    root["application"] = "Snippet Manager";
    root["formatVersion"] = 1;
    root["snippets"] = array;
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

// Reads the first of several possible field names
static QJsonValue field(const QJsonObject &object, std::initializer_list<const char *> names)
{
    for (const char *name : names) {
        auto it = object.constFind(QLatin1String(name));
        if (it != object.constEnd()) return *it;
    }
    return QJsonValue();
}

QList<Snippet> fromJson(const QByteArray &data, QString *errorMessage)
{
    QJsonParseError parseError;
    QJsonDocument document = QJsonDocument::fromJson(data, &parseError);
    if (document.isNull()) {
        if (errorMessage) *errorMessage = "The file isn't valid JSON: " + parseError.errorString();
        return {};
    }

    // Either {"snippets": [...]} or just [...]
    QJsonArray array = document.isArray() ? document.array() : document.object().value("snippets").toArray();
    if (array.isEmpty() && document.isObject() && !document.object().contains("snippets")) {
        if (errorMessage) *errorMessage = "The JSON file doesn't contain a list of snippets.";
        return {};
    }

    QList<Snippet> snippets;
    for (const QJsonValue &value : array) {
        if (!value.isObject()) continue;
        QJsonObject object = value.toObject();
        Snippet snippet;
        snippet.title = field(object, {"title", "name"}).toString().trimmed();
        snippet.content = field(object, {"content", "body", "text", "snippet"}).toString();
        if (snippet.title.isEmpty() && snippet.content.isEmpty()) continue;
        if (snippet.title.isEmpty()) snippet.title = snippet.content.section('\n', 0, 0).left(60).trimmed();

        QJsonValue tags = field(object, {"tags", "tag", "category"});
        if (tags.isArray()) {
            QStringList list;
            for (const QJsonValue &tag : tags.toArray()) list << tag.toString();
            snippet.tags = Database::normalizeTags(list);
        } else {
            snippet.tags = splitTags(tags.toString());
        }
        applyType(snippet, field(object, {"type", "format"}).toString(), field(object, {"language", "lang"}).toString());
        snippet.folderPath = field(object, {"folder", "path"}).toString().trimmed();
        snippet.created = parseTime(field(object, {"created", "createdAt"}).toString());
        snippet.modified = parseTime(field(object, {"modified", "updated", "modifiedAt"}).toString());
        snippets.append(snippet);
    }
    return snippets;
}

// ---------------------------------------------------------------------------
// Markdown
// ---------------------------------------------------------------------------

static const char *MetadataPrefix = "<!-- snippet: ";

QString toMarkdown(const QList<Snippet> &snippets)
{
    QString out = "# Snippets\n\nExported from Snippet Manager. Each snippet is a section; "
                  "its details are in the comment under its title.\n";
    for (const Snippet &snippet : snippets) {
        QJsonObject details;
        details["type"] = typeName(snippet);
        if (!snippet.language.isEmpty()) details["language"] = snippet.language;
        if (!snippet.tags.isEmpty()) details["tags"] = QJsonArray::fromStringList(snippet.tags);
        if (!snippet.folderPath.isEmpty()) details["folder"] = snippet.folderPath;
        if (snippet.created.isValid()) details["created"] = isoTime(snippet.created);
        if (snippet.modified.isValid()) details["modified"] = isoTime(snippet.modified);
        QString json = QString::fromUtf8(QJsonDocument(details).toJson(QJsonDocument::Compact));
        // "--" would end the HTML comment early
        json.replace("--", "-\\u002d");

        // A fence longer than any run of backticks in the content
        int longestRun = 0, run = 0;
        for (QChar c : snippet.content) {
            run = c == '`' ? run + 1 : 0;
            longestRun = qMax(longestRun, run);
        }
        QString fence(qMax(3, longestRun + 1), '`');
        // Fence info can't contain spaces ("INI Files" becomes "ini-files")
        QString info = snippet.isRich() ? "markdown" : snippet.language.toLower().replace(' ', '-');

        out += "\n## " + QString(snippet.title).replace('\n', ' ') + "\n\n";
        out += MetadataPrefix + json + " -->\n\n";
        out += fence + info + "\n" + snippet.content + "\n" + fence + "\n";
    }
    return out;
}

QList<Snippet> fromMarkdown(const QString &text, QString *errorMessage)
{
    QStringList lines = QString(text).replace("\r\n", "\n").split('\n');

    // Snippets are "## " sections; files with only "# " headings use those
    QString headingPrefix = "## ";
    bool hasLevelTwo = false;
    for (const QString &line : lines) {
        if (line.startsWith("## ")) { hasLevelTwo = true; break; }
    }
    if (!hasLevelTwo) headingPrefix = "# ";

    static const QRegularExpression fenceStart("^(`{3,}|~{3,})\\s*([^`\\s]*)\\s*$");
    QList<Snippet> snippets;
    int i = 0;
    while (i < lines.size()) {
        if (!lines[i].startsWith(headingPrefix)) { ++i; continue; }

        Snippet snippet;
        snippet.title = lines[i].mid(headingPrefix.size()).trimmed();
        ++i;
        QJsonObject details;
        QStringList body;
        bool fenced = false;
        while (i < lines.size() && !lines[i].startsWith(headingPrefix)
               && !(headingPrefix == "## " && lines[i].startsWith("# "))) {
            const QString &line = lines[i];
            if (line.startsWith(MetadataPrefix) && line.trimmed().endsWith("-->") && details.isEmpty() && body.join("").trimmed().isEmpty()) {
                QString json = line.trimmed().mid(QString(MetadataPrefix).size());
                json.chop(3);
                details = QJsonDocument::fromJson(json.trimmed().toUtf8()).object();
                ++i;
                continue;
            }
            QRegularExpressionMatch match = fenceStart.match(line);
            if (!fenced && body.join("").trimmed().isEmpty() && match.hasMatch()) {
                // A fenced block: everything up to the matching closing fence
                QString fence = match.captured(1);
                QString info = match.captured(2);
                if (!details.contains("type") && !details.contains("language")) {
                    if (info.compare("markdown", Qt::CaseInsensitive) == 0 || info.compare("md", Qt::CaseInsensitive) == 0) {
                        details["language"] = "Markdown";
                    } else {
                        details["language"] = info;
                    }
                }
                QStringList code;
                ++i;
                while (i < lines.size()) {
                    QString trimmed = lines[i].trimmed();
                    if (trimmed.size() >= fence.size() && trimmed == QString(trimmed.size(), fence.at(0))) break;
                    code << lines[i];
                    ++i;
                }
                ++i;  // the closing fence
                body = code;
                fenced = true;
                continue;
            }
            if (!fenced) body << line;
            ++i;
        }

        snippet.content = fenced ? body.join('\n') : body.join('\n').trimmed();
        if (snippet.title.isEmpty() && snippet.content.isEmpty()) continue;
        if (snippet.title.isEmpty()) snippet.title = "Untitled";
        QStringList tags;
        for (const QJsonValue &tag : details.value("tags").toArray()) tags << tag.toString();
        snippet.tags = Database::normalizeTags(tags);
        applyType(snippet, details.value("type").toString(), details.value("language").toString());
        snippet.folderPath = details.value("folder").toString().trimmed();
        snippet.created = parseTime(details.value("created").toString());
        snippet.modified = parseTime(details.value("modified").toString());
        snippets.append(snippet);
    }

    if (snippets.isEmpty() && errorMessage) {
        *errorMessage = "No snippets were found. Each snippet should start with a \"## Title\" heading.";
    }
    return snippets;
}

// ---------------------------------------------------------------------------
// CSV
// ---------------------------------------------------------------------------

static QString csvField(const QString &value)
{
    if (value.contains(QRegularExpression("[\",\\r\\n]")) || value.startsWith(' ') || value.endsWith(' ')) {
        return "\"" + QString(value).replace("\"", "\"\"") + "\"";
    }
    return value;
}

QString toCsv(const QList<Snippet> &snippets)
{
    QString out = "title,content,tags,type,language,folder,created,modified\r\n";
    for (const Snippet &snippet : snippets) {
        QStringList row = {snippet.title, snippet.content, snippet.tags.join(", "), typeName(snippet),
                           snippet.language, snippet.folderPath, isoTime(snippet.created), isoTime(snippet.modified)};
        for (QString &value : row) value = csvField(value);
        out += row.join(',') + "\r\n";
    }
    return out;
}

// Splits CSV text into rows of fields (RFC 4180: quoted fields may contain
// separators, doubled quotes and line breaks)
static QList<QStringList> parseCsv(const QString &text, QChar separator)
{
    QList<QStringList> rows;
    QStringList row;
    QString value;
    bool quoted = false;
    bool fieldStarted = false;
    for (int i = 0; i < text.size(); ++i) {
        QChar c = text.at(i);
        if (quoted) {
            if (c == '"') {
                if (i + 1 < text.size() && text.at(i + 1) == '"') {
                    value += '"';
                    ++i;
                } else {
                    quoted = false;
                }
            } else {
                value += c;
            }
        } else if (c == '"' && value.trimmed().isEmpty()) {
            quoted = true;
            value.clear();
            fieldStarted = true;
        } else if (c == separator) {
            row << value;
            value.clear();
            fieldStarted = false;
        } else if (c == '\n' || c == '\r') {
            if (c == '\r' && i + 1 < text.size() && text.at(i + 1) == '\n') ++i;
            row << value;
            if (!(row.size() == 1 && row[0].isEmpty() && !fieldStarted)) rows << row;
            row.clear();
            value.clear();
            fieldStarted = false;
        } else {
            value += c;
            fieldStarted = true;
        }
    }
    if (!value.isEmpty() || !row.isEmpty() || fieldStarted) {
        row << value;
        rows << row;
    }
    return rows;
}

// Picks the separator used most in the first line (outside quotes)
static QChar detectSeparator(const QString &text)
{
    int counts[3] = {0, 0, 0};
    const QChar candidates[3] = {',', ';', '\t'};
    bool quoted = false;
    for (QChar c : text) {
        if (c == '"') quoted = !quoted;
        if (!quoted && (c == '\n' || c == '\r')) break;
        if (quoted) continue;
        for (int k = 0; k < 3; ++k) {
            if (c == candidates[k]) ++counts[k];
        }
    }
    int best = 0;
    for (int k = 1; k < 3; ++k) {
        if (counts[k] > counts[best]) best = k;
    }
    return candidates[best];
}

QList<Snippet> fromCsv(const QString &input, QString *errorMessage)
{
    QString text = input;
    if (text.startsWith(QChar(0xFEFF))) text.remove(0, 1);  // byte order mark
    QList<QStringList> rows = parseCsv(text, detectSeparator(text));
    if (rows.isEmpty()) {
        if (errorMessage) *errorMessage = "The CSV file is empty.";
        return {};
    }

    // Work out which column is which from the header row
    static const QHash<QString, QString> columnNames = {
        {"title", "title"}, {"name", "title"}, {"snippet name", "title"}, {"abbreviation", "title"},
        {"trigger", "title"}, {"key", "title"}, {"label", "title"},
        {"content", "content"}, {"body", "content"}, {"text", "content"}, {"snippet", "content"},
        {"value", "content"}, {"expansion", "content"}, {"phrase", "content"}, {"replacement", "content"},
        {"tags", "tags"}, {"tag", "tags"}, {"labels", "tags"}, {"category", "tags"}, {"categories", "tags"},
        {"group", "tags"},
        {"folder", "folder"}, {"path", "folder"}, {"directory", "folder"},
        {"type", "type"}, {"format", "type"},
        {"language", "language"}, {"lang", "language"}, {"syntax", "language"},
        {"created", "created"}, {"created at", "created"}, {"date", "created"},
        {"modified", "modified"}, {"updated", "modified"}, {"modified at", "modified"}, {"updated at", "modified"},
    };
    QHash<QString, int> columns;
    const QStringList &header = rows.first();
    for (int c = 0; c < header.size(); ++c) {
        QString name = header[c].trimmed().toLower().replace('_', ' ');
        if (columnNames.contains(name) && !columns.contains(columnNames.value(name))) {
            columns.insert(columnNames.value(name), c);
        }
    }
    bool hasHeader = columns.contains("title") || columns.contains("content");
    if (hasHeader) {
        rows.removeFirst();
    } else {
        // No recognisable header: title then content (or just content)
        columns.clear();
        if (header.size() >= 2) {
            columns.insert("title", 0);
            columns.insert("content", 1);
        } else {
            columns.insert("content", 0);
        }
    }

    auto value = [&columns](const QStringList &row, const QString &column) {
        int index = columns.value(column, -1);
        return index >= 0 && index < row.size() ? row.at(index) : QString();
    };

    QList<Snippet> snippets;
    for (const QStringList &row : rows) {
        Snippet snippet;
        snippet.title = value(row, "title").trimmed();
        snippet.content = value(row, "content");
        if (snippet.title.isEmpty() && snippet.content.trimmed().isEmpty()) continue;
        if (snippet.title.isEmpty()) snippet.title = snippet.content.section('\n', 0, 0).left(60).trimmed();
        snippet.tags = splitTags(value(row, "tags"));
        applyType(snippet, value(row, "type"), value(row, "language"));
        snippet.folderPath = value(row, "folder").trimmed();
        snippet.created = parseTime(value(row, "created"));
        snippet.modified = parseTime(value(row, "modified"));
        snippets.append(snippet);
    }
    if (snippets.isEmpty() && errorMessage) {
        *errorMessage = "No snippets were found in the CSV file.";
    }
    return snippets;
}

// ---------------------------------------------------------------------------
// Files
// ---------------------------------------------------------------------------

QList<Snippet> readFile(const QString &path, QString *errorMessage)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMessage) *errorMessage = file.errorString();
        return {};
    }
    QByteArray data = file.readAll();
    switch (formatForPath(path)) {
    case Format::Json:
        return fromJson(data, errorMessage);
    case Format::Markdown:
        return fromMarkdown(QString::fromUtf8(data), errorMessage);
    case Format::Csv:
        return fromCsv(QString::fromUtf8(data), errorMessage);
    case Format::Unknown:
        break;
    }
    if (errorMessage) *errorMessage = "Unsupported file type. Use a .json, .md or .csv file.";
    return {};
}

bool writeFile(const QString &path, const QList<Snippet> &snippets, QString *errorMessage)
{
    QByteArray data;
    switch (formatForPath(path)) {
    case Format::Json:
        data = toJson(snippets);
        break;
    case Format::Markdown:
        data = toMarkdown(snippets).toUtf8();
        break;
    case Format::Csv:
        // The byte order mark helps spreadsheet apps detect UTF-8
        data = "\xEF\xBB\xBF" + toCsv(snippets).toUtf8();
        break;
    case Format::Unknown:
        if (errorMessage) *errorMessage = "Unsupported file type. Use a .json, .md or .csv file.";
        return false;
    }

    // Written to a temporary file first, so a failed export can't leave a half-written file
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (errorMessage) *errorMessage = file.errorString();
        return false;
    }
    // Snippets often hold passwords and tokens, so keep the export private
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    file.write(data);
    if (!file.commit()) {
        if (errorMessage) *errorMessage = file.errorString();
        return false;
    }
    return true;
}

} // namespace ImportExport
