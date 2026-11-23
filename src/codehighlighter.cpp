#include "codehighlighter.h"
#include <QHash>
#include <QPalette>
#include <QRegularExpression>
#include <QSet>

// How a language is highlighted
enum class Style { Code, Markup, Markdown, Diff, Ini, Yaml };

struct LanguageSpec {
    Style style = Style::Code;
    QStringList lineComments;       // e.g. "//", "#", "--"
    QString blockStart, blockEnd;   // e.g. "/*" and "*/"
    QString quotes = "\"'";         // characters that start strings
    bool tripleQuotes = false;      // Python's """ and '''
    bool caseInsensitive = false;   // keywords in any case (SQL, CMake...)
    QSet<QString> keywords;
    QSet<QString> types;            // built-in types and constants
    QChar variablePrefix;           // '$' for shells, PHP, Perl...
    bool decorators = false;        // @name (Python, Java, Kotlin, C#, Swift, TypeScript)
    bool preprocessor = false;      // # directives (C, C++, C#)
    bool keysBeforeColon = false;   // "key": (JSON)
    bool functionCalls = true;      // name( is a call
};

// Block states, for comments and strings that continue onto the next line
enum BlockState { Normal = 0, InBlockComment = 1, InTripleDouble = 2, InTripleSingle = 3, InFence = 4 };

static QSet<QString> words(const char *list)
{
    QSet<QString> result;
    for (const QString &word : QString::fromLatin1(list).split(' ', Qt::SkipEmptyParts)) result.insert(word);
    return result;
}

static const QHash<QString, LanguageSpec> &specs()
{
    static const QHash<QString, LanguageSpec> table = [] {
        QHash<QString, LanguageSpec> t;
        LanguageSpec c;
        c.lineComments = {"//"};
        c.blockStart = "/*";
        c.blockEnd = "*/";

        LanguageSpec cLang = c;
        cLang.preprocessor = true;
        cLang.keywords = words("auto break case const continue default do else enum extern for goto if inline "
                               "register restrict return sizeof static struct switch typedef union volatile while");
        cLang.types = words("void char short int long float double signed unsigned bool size_t NULL true false "
                            "int8_t int16_t int32_t int64_t uint8_t uint16_t uint32_t uint64_t FILE");
        t.insert("C", cLang);

        LanguageSpec cpp = cLang;
        cpp.keywords += words("alignas alignof catch class concept consteval constexpr constinit co_await co_return "
                              "co_yield decltype delete explicit export friend mutable namespace new noexcept "
                              "operator override final private protected public requires static_assert "
                              "template this throw try typeid typename using virtual");
        cpp.types += words("nullptr wchar_t char8_t char16_t char32_t std string vector map");
        t.insert("C++", cpp);

        LanguageSpec cs = c;
        cs.preprocessor = true;
        cs.decorators = false;
        cs.keywords = words("abstract as async await base break case catch checked class const continue default "
                            "delegate do else enum event explicit extern finally fixed for foreach get goto if "
                            "implicit in init interface internal is lock namespace new operator out override "
                            "params private protected public readonly record ref return sealed set sizeof "
                            "stackalloc static struct switch this throw try typeof unchecked unsafe using var "
                            "virtual volatile when where while yield");
        cs.types = words("bool byte char decimal double dynamic float int long object sbyte short string uint "
                         "ulong ushort void true false null");
        t.insert("C#", cs);

        LanguageSpec java = c;
        java.decorators = true;
        java.keywords = words("abstract assert break case catch class const continue default do else enum extends "
                              "final finally for goto if implements import instanceof interface native new package "
                              "private protected public record return sealed static strictfp super switch "
                              "synchronized this throw throws transient try var void volatile while yield");
        java.types = words("boolean byte char double float int long short String Object true false null");
        t.insert("Java", java);

        LanguageSpec kotlin = c;
        kotlin.decorators = true;
        kotlin.keywords = words("as break class continue do else for fun if in interface is object package return "
                                "super this throw try typealias typeof val var when while by catch constructor "
                                "data enum finally get import init internal lateinit open override private "
                                "protected public sealed set suspend companion");
        kotlin.types = words("Any Boolean Byte Char Double Float Int Long Nothing Short String Unit true false null");
        t.insert("Kotlin", kotlin);

        LanguageSpec js = c;
        js.quotes = "\"'`";
        js.keywords = words("async await break case catch class const continue debugger default delete do else "
                            "export extends finally for from function if import in instanceof let new of return "
                            "static super switch this throw try typeof var void while with yield get set");
        js.types = words("true false null undefined NaN Infinity console window document Array Object String "
                         "Number Boolean Promise Map Set JSON Math");
        t.insert("JavaScript", js);

        LanguageSpec ts = js;
        ts.decorators = true;
        ts.keywords += words("abstract as declare enum implements interface keyof namespace private protected "
                             "public readonly type satisfies is infer");
        ts.types += words("any boolean never number object string symbol unknown void bigint");
        t.insert("TypeScript", ts);

        LanguageSpec go = c;
        go.quotes = "\"'`";
        go.keywords = words("break case chan const continue default defer else fallthrough for func go goto if "
                            "import interface map package range return select struct switch type var");
        go.types = words("bool byte complex64 complex128 error float32 float64 int int8 int16 int32 int64 rune "
                         "string uint uint8 uint16 uint32 uint64 uintptr true false nil iota any");
        t.insert("Go", go);

        LanguageSpec rust = c;
        rust.quotes = "\"";
        rust.keywords = words("as async await break const continue crate dyn else enum extern fn for if impl in "
                              "let loop match mod move mut pub ref return self Self static struct super trait "
                              "type unsafe use where while macro_rules");
        rust.types = words("bool char f32 f64 i8 i16 i32 i64 i128 isize u8 u16 u32 u64 u128 usize str String "
                           "Vec Option Result Some None Ok Err Box true false");
        t.insert("Rust", rust);

        LanguageSpec swift = c;
        swift.quotes = "\"";
        swift.decorators = true;
        swift.keywords = words("associatedtype break case catch class continue default defer deinit do else enum "
                               "extension fallthrough fileprivate for func guard if import in init inout internal "
                               "is let open operator private protocol public repeat rethrows return self Self "
                               "static struct subscript super switch throw throws try typealias var where while "
                               "async await some any");
        swift.types = words("Bool Character Double Float Int Int8 Int16 Int32 Int64 String UInt Void Array "
                            "Dictionary Set Optional true false nil");
        t.insert("Swift", swift);

        LanguageSpec php = c;
        php.lineComments = {"//", "#"};
        php.variablePrefix = '$';
        php.keywords = words("abstract and array as break callable case catch class clone const continue declare "
                             "default do echo else elseif empty enddeclare endfor endforeach endif endswitch "
                             "endwhile extends final finally fn for foreach function global goto if implements "
                             "include include_once instanceof insteadof interface isset list match namespace new "
                             "or print private protected public readonly require require_once return static "
                             "switch throw trait try unset use var while xor yield");
        php.types = words("true false null int float string bool array object mixed void self parent");
        t.insert("PHP", php);

        LanguageSpec css;
        css.blockStart = "/*";
        css.blockEnd = "*/";
        css.keywords = words("important media import keyframes font-face supports charset");
        css.types = words("inherit initial unset none auto block inline flex grid absolute relative fixed sticky "
                          "solid dashed bold normal italic px em rem vh vw");
        css.functionCalls = true;
        t.insert("CSS", css);

        LanguageSpec shell;
        shell.lineComments = {"#"};
        shell.variablePrefix = '$';
        shell.functionCalls = false;
        shell.keywords = words("if then else elif fi case esac for select while until do done in function time "
                               "coproc return exit break continue local export readonly declare typeset unset "
                               "shift source alias set trap eval exec");
        shell.types = words("echo printf cd pwd test true false read grep sed awk find xargs cat ls rm cp mv "
                            "mkdir chmod chown sudo kill");
        t.insert("Bash", shell);
        t.insert("Zsh", shell);
        LanguageSpec fish = shell;
        fish.keywords += words("end begin not and or set_color switch");
        t.insert("Fish", fish);

        LanguageSpec ps;
        ps.lineComments = {"#"};
        ps.blockStart = "<#";
        ps.blockEnd = "#>";
        ps.variablePrefix = '$';
        ps.caseInsensitive = true;
        ps.keywords = words("begin break catch class continue data define do dynamicparam else elseif end exit "
                            "filter finally for foreach from function if in param process return switch throw "
                            "trap try until using var while");
        ps.types = words("true false null");
        t.insert("PowerShell", ps);

        LanguageSpec python;
        python.lineComments = {"#"};
        python.tripleQuotes = true;
        python.decorators = true;
        python.keywords = words("and as assert async await break class continue def del elif else except finally "
                                "for from global if import in is lambda nonlocal not or pass raise return try "
                                "while with yield match case");
        python.types = words("True False None self cls int float str bool list dict set tuple bytes object "
                             "print len range open super isinstance Exception");
        t.insert("Python", python);

        LanguageSpec ruby;
        ruby.lineComments = {"#"};
        ruby.keywords = words("alias and begin break case class def defined? do else elsif end ensure for if in "
                              "module next not or redo rescue retry return self super then undef unless until "
                              "when while yield require require_relative attr_accessor attr_reader puts");
        ruby.types = words("true false nil Integer String Array Hash Symbol");
        t.insert("Ruby", ruby);

        LanguageSpec perl;
        perl.lineComments = {"#"};
        perl.variablePrefix = '$';
        perl.keywords = words("my our local sub if elsif else unless while until for foreach do last next redo "
                              "return use no package require print printf die warn eval and or not");
        t.insert("Perl", perl);

        LanguageSpec lua;
        lua.lineComments = {"--"};
        lua.blockStart = "--[[";
        lua.blockEnd = "]]";
        lua.keywords = words("and break do else elseif end for function goto if in local not or repeat return "
                             "then until while");
        lua.types = words("true false nil self print pairs ipairs table string math require");
        t.insert("Lua", lua);

        LanguageSpec sql;
        sql.lineComments = {"--"};
        sql.blockStart = "/*";
        sql.blockEnd = "*/";
        sql.caseInsensitive = true;
        sql.functionCalls = false;
        sql.keywords = words("select from where and or not insert into values update set delete create table "
                             "drop alter add column index view join left right inner outer full cross on as "
                             "group by order having limit offset union all distinct case when then else end "
                             "is null like in between exists primary key foreign references default unique "
                             "begin commit rollback transaction with returning asc desc if");
        sql.types = words("int integer bigint smallint text varchar char boolean date datetime timestamp real "
                          "float double decimal numeric blob true false count sum avg min max coalesce");
        t.insert("SQL", sql);

        LanguageSpec cmake;
        cmake.lineComments = {"#"};
        cmake.quotes = "\"";
        cmake.caseInsensitive = true;
        cmake.variablePrefix = '$';
        cmake.keywords = words("if elseif else endif foreach endforeach while endwhile function endfunction "
                               "macro endmacro return set unset option project add_executable add_library "
                               "target_link_libraries target_include_directories target_compile_definitions "
                               "find_package include install message cmake_minimum_required add_subdirectory");
        cmake.types = words("on off true false yes no and or not public private interface required");
        t.insert("CMake", cmake);

        LanguageSpec make;
        make.lineComments = {"#"};
        make.variablePrefix = '$';
        make.functionCalls = false;
        make.keywords = words("ifeq ifneq ifdef ifndef else endif include define endef export override .PHONY");
        t.insert("Makefile", make);

        LanguageSpec docker;
        docker.lineComments = {"#"};
        docker.variablePrefix = '$';
        docker.caseInsensitive = true;
        docker.functionCalls = false;
        docker.keywords = words("from run cmd label maintainer expose env add copy entrypoint volume user "
                                "workdir arg onbuild stopsignal healthcheck shell as");
        t.insert("Dockerfile", docker);

        LanguageSpec json;
        json.quotes = "\"";
        json.keysBeforeColon = true;
        json.functionCalls = false;
        json.types = words("true false null");
        t.insert("JSON", json);

        LanguageSpec markup;
        markup.style = Style::Markup;
        t.insert("HTML", markup);
        t.insert("XML", markup);

        LanguageSpec markdown;
        markdown.style = Style::Markdown;
        t.insert("Markdown", markdown);

        LanguageSpec diff;
        diff.style = Style::Diff;
        t.insert("Diff", diff);

        LanguageSpec ini;
        ini.style = Style::Ini;
        t.insert("INI Files", ini);
        t.insert("TOML", ini);

        LanguageSpec yaml;
        yaml.style = Style::Yaml;
        t.insert("YAML", yaml);
        return t;
    }();
    return table;
}

CodeHighlighter::CodeHighlighter(QObject *parent)
    : QSyntaxHighlighter(parent)
{
}

bool CodeHighlighter::supports(const QString &language)
{
    return specs().contains(language);
}

void CodeHighlighter::setLanguage(const QString &language, const QPalette &palette)
{
    auto it = specs().constFind(language);
    m_spec = it != specs().constEnd() ? &it.value() : nullptr;

    // Colours that read well on the editor's background
    bool dark = palette.color(QPalette::Base).lightness() < 128;
    auto format = [](const QColor &color, bool bold = false, bool italic = false) {
        QTextCharFormat f;
        f.setForeground(color);
        if (bold) f.setFontWeight(QFont::Bold);
        f.setFontItalic(italic);
        return f;
    };
    m_keyword = format(dark ? QColor("#c792ea") : QColor("#8e24aa"), true);
    m_type = format(dark ? QColor("#82aaff") : QColor("#1565c0"));
    m_string = format(dark ? QColor("#c3e88d") : QColor("#2e7d32"));
    m_comment = format(dark ? QColor("#7f8c98") : QColor("#78909c"), false, true);
    m_number = format(dark ? QColor("#f78c6c") : QColor("#d84315"));
    m_function = format(dark ? QColor("#ffcb6b") : QColor("#6d4c41"));
    m_preprocessor = format(dark ? QColor("#89ddff") : QColor("#00838f"));
    m_variable = format(dark ? QColor("#f07178") : QColor("#c62828"));
    m_tag = format(dark ? QColor("#f07178") : QColor("#1565c0"), true);
    m_attribute = format(dark ? QColor("#ffcb6b") : QColor("#8e24aa"));
    m_heading = format(dark ? QColor("#82aaff") : QColor("#1565c0"), true);
    m_key = format(dark ? QColor("#82aaff") : QColor("#1565c0"));
    m_added = format(dark ? QColor("#c3e88d") : QColor("#2e7d32"));
    m_added.setBackground(dark ? QColor(46, 125, 50, 60) : QColor(46, 125, 50, 30));
    m_removed = format(dark ? QColor("#f07178") : QColor("#c62828"));
    m_removed.setBackground(dark ? QColor(198, 40, 40, 60) : QColor(198, 40, 40, 30));
    rehighlight();
}

void CodeHighlighter::highlightBlock(const QString &text)
{
    setCurrentBlockState(Normal);
    if (!m_spec) return;
    switch (m_spec->style) {
    case Style::Code: highlightCode(text); break;
    case Style::Markup: highlightMarkup(text); break;
    case Style::Markdown: highlightMarkdown(text); break;
    case Style::Diff: highlightDiff(text); break;
    case Style::Ini: highlightIni(text); break;
    case Style::Yaml: highlightYaml(text); break;
    }
}

void CodeHighlighter::applyPattern(const QString &text, const QString &pattern, const QTextCharFormat &format, int group)
{
    QRegularExpression regex(pattern);
    QRegularExpressionMatchIterator it = regex.globalMatch(text);
    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        if (match.capturedStart(group) >= 0) {
            setFormat(match.capturedStart(group), match.capturedLength(group), format);
        }
    }
}

// Returns the position just after the string starting at start (or the end of the line)
int CodeHighlighter::scanString(const QString &text, int start, const QString &quote)
{
    int i = start + quote.size();
    while (i < text.size()) {
        if (text.at(i) == '\\') {
            i += 2;
            continue;
        }
        if (text.mid(i, quote.size()) == quote) {
            return i + quote.size();
        }
        ++i;
    }
    return text.size();
}

void CodeHighlighter::highlightCode(const QString &text)
{
    const LanguageSpec &spec = *m_spec;
    int i = 0;

    // Carry on a comment or triple-quoted string from the previous line
    int previous = previousBlockState();
    if (previous == InBlockComment) {
        int end = text.indexOf(spec.blockEnd);
        if (end < 0) {
            setFormat(0, text.size(), m_comment);
            setCurrentBlockState(InBlockComment);
            return;
        }
        i = end + spec.blockEnd.size();
        setFormat(0, i, m_comment);
    } else if (previous == InTripleDouble || previous == InTripleSingle) {
        QString quote = previous == InTripleDouble ? "\"\"\"" : "'''";
        int end = text.indexOf(quote);
        if (end < 0) {
            setFormat(0, text.size(), m_string);
            setCurrentBlockState(previous);
            return;
        }
        i = end + 3;
        setFormat(0, i, m_string);
    }

    // Preprocessor directives take the whole line
    if (spec.preprocessor && text.trimmed().startsWith('#')) {
        setFormat(0, text.size(), m_preprocessor);
        return;
    }

    auto isWordStart = [](QChar c) { return c.isLetter() || c == '_'; };
    auto isWordChar = [](QChar c) { return c.isLetterOrNumber() || c == '_'; };

    while (i < text.size()) {
        QChar c = text.at(i);

        // Comments
        if (!spec.blockStart.isEmpty() && text.mid(i, spec.blockStart.size()) == spec.blockStart) {
            int end = text.indexOf(spec.blockEnd, i + spec.blockStart.size());
            if (end < 0) {
                setFormat(i, text.size() - i, m_comment);
                setCurrentBlockState(InBlockComment);
                return;
            }
            setFormat(i, end + spec.blockEnd.size() - i, m_comment);
            i = end + spec.blockEnd.size();
            continue;
        }
        bool lineComment = false;
        for (const QString &marker : spec.lineComments) {
            if (text.mid(i, marker.size()) == marker) {
                // In shells, # only starts a comment at the start of a word ($# and a#b aren't comments)
                if (marker == "#" && i > 0 && !text.at(i - 1).isSpace() && spec.variablePrefix == '$') continue;
                lineComment = true;
                break;
            }
        }
        if (lineComment) {
            setFormat(i, text.size() - i, m_comment);
            return;
        }

        // Strings, including Python's triple-quoted ones
        if (spec.tripleQuotes && (text.mid(i, 3) == "\"\"\"" || text.mid(i, 3) == "'''")) {
            QString quote = text.mid(i, 3);
            int end = text.indexOf(quote, i + 3);
            if (end < 0) {
                setFormat(i, text.size() - i, m_string);
                setCurrentBlockState(quote == "\"\"\"" ? InTripleDouble : InTripleSingle);
                return;
            }
            setFormat(i, end + 3 - i, m_string);
            i = end + 3;
            continue;
        }
        if (spec.quotes.contains(c)) {
            int end = scanString(text, i, QString(c));
            bool isKey = false;
            if (spec.keysBeforeColon) {
                int next = end;
                while (next < text.size() && text.at(next).isSpace()) ++next;
                isKey = next < text.size() && text.at(next) == ':';
            }
            setFormat(i, end - i, isKey ? m_key : m_string);
            i = end;
            continue;
        }

        // Variables ($name, ${name}, $(name))
        if (!spec.variablePrefix.isNull() && c == spec.variablePrefix && i + 1 < text.size()) {
            int end = i + 1;
            QChar next = text.at(end);
            if (next == '{' || next == '(') {
                QChar close = next == '{' ? '}' : ')';
                int closing = text.indexOf(close, end);
                end = closing < 0 ? text.size() : closing + 1;
            } else {
                while (end < text.size() && (isWordChar(text.at(end)) || (end == i + 1 && QString("#?@*!$-").contains(text.at(end))))) ++end;
            }
            if (end > i + 1) {
                setFormat(i, end - i, m_variable);
                i = end;
                continue;
            }
        }

        // Decorators and annotations
        if (spec.decorators && c == '@' && i + 1 < text.size() && isWordStart(text.at(i + 1))) {
            int end = i + 1;
            while (end < text.size() && (isWordChar(text.at(end)) || text.at(end) == '.')) ++end;
            setFormat(i, end - i, m_preprocessor);
            i = end;
            continue;
        }

        // Numbers
        if (c.isDigit() && (i == 0 || !isWordChar(text.at(i - 1)))) {
            int end = i + 1;
            while (end < text.size() && (text.at(end).isLetterOrNumber() || text.at(end) == '.' || text.at(end) == '_')) ++end;
            setFormat(i, end - i, m_number);
            i = end;
            continue;
        }

        // Words: keywords, types, function calls
        if (isWordStart(c)) {
            int end = i + 1;
            while (end < text.size() && (isWordChar(text.at(end)) || (text.at(end) == '?' && end + 1 == text.size()))) ++end;
            QString word = text.mid(i, end - i);
            QString key = spec.caseInsensitive ? word.toLower() : word;
            if (spec.keywords.contains(key)) {
                setFormat(i, end - i, m_keyword);
            } else if (spec.types.contains(key)) {
                setFormat(i, end - i, m_type);
            } else if (spec.functionCalls) {
                int next = end;
                while (next < text.size() && text.at(next) == ' ') ++next;
                if (next < text.size() && text.at(next) == '(') setFormat(i, end - i, m_function);
            }
            i = end;
            continue;
        }
        ++i;
    }
}

void CodeHighlighter::highlightMarkup(const QString &text)
{
    // Tags and their attributes, then strings, then comments over everything
    applyPattern(text, R"(</?[A-Za-z][\w:.-]*|/?>)", m_tag);
    applyPattern(text, R"(\s([A-Za-z_:][\w:.-]*)\s*=)", m_attribute, 1);
    applyPattern(text, R"("[^"]*"|'[^']*')", m_string);
    applyPattern(text, R"(&[A-Za-z#0-9]+;)", m_type);
    applyPattern(text, R"(<\?[\w-]+|\?>|<!DOCTYPE[^>]*>)", m_preprocessor);

    int start = 0;
    if (previousBlockState() == InBlockComment) {
        int end = text.indexOf("-->");
        if (end < 0) {
            setFormat(0, text.size(), m_comment);
            setCurrentBlockState(InBlockComment);
            return;
        }
        setFormat(0, end + 3, m_comment);
        start = end + 3;
    }
    while ((start = text.indexOf("<!--", start)) >= 0) {
        int end = text.indexOf("-->", start + 4);
        if (end < 0) {
            setFormat(start, text.size() - start, m_comment);
            setCurrentBlockState(InBlockComment);
            return;
        }
        setFormat(start, end + 3 - start, m_comment);
        start = end + 3;
    }
}

void CodeHighlighter::highlightMarkdown(const QString &text)
{
    // Fenced code blocks are shown as code
    bool inFence = previousBlockState() == InFence;
    if (text.trimmed().startsWith("```") || text.trimmed().startsWith("~~~")) {
        setFormat(0, text.size(), m_preprocessor);
        if (!inFence) setCurrentBlockState(InFence);
        return;
    }
    if (inFence) {
        setFormat(0, text.size(), m_string);
        setCurrentBlockState(InFence);
        return;
    }
    applyPattern(text, R"(^#{1,6}\s.*$)", m_heading);
    applyPattern(text, R"(^\s*([-*+]|\d+[.)])\s)", m_keyword, 1);
    applyPattern(text, R"(^\s*>.*$)", m_comment);
    applyPattern(text, R"((\*\*|__)[^*_]+\1)", m_type);
    applyPattern(text, R"(`[^`]+`)", m_string);
    applyPattern(text, R"(\[[^\]]*\]\([^)]*\))", m_function);
}

void CodeHighlighter::highlightDiff(const QString &text)
{
    if (text.startsWith("+++") || text.startsWith("---") || text.startsWith("diff ") || text.startsWith("index ")) {
        setFormat(0, text.size(), m_heading);
    } else if (text.startsWith("@@")) {
        setFormat(0, text.size(), m_preprocessor);
    } else if (text.startsWith('+')) {
        setFormat(0, text.size(), m_added);
    } else if (text.startsWith('-')) {
        setFormat(0, text.size(), m_removed);
    }
}

void CodeHighlighter::highlightIni(const QString &text)
{
    QString trimmed = text.trimmed();
    if (trimmed.startsWith('#') || trimmed.startsWith(';')) {
        setFormat(0, text.size(), m_comment);
        return;
    }
    applyPattern(text, R"(^\s*\[\[?[^\]]*\]\]?)", m_heading);
    applyPattern(text, R"(^\s*([\w.\-"]+)\s*=)", m_key, 1);
    applyPattern(text, R"(=\s*(-?\d[\d._:eE+-]*)\s*$)", m_number, 1);
    applyPattern(text, R"(\b(true|false)\b)", m_type);
    applyPattern(text, R"("[^"]*"|'[^']*')", m_string);
    applyPattern(text, R"(\s#.*$)", m_comment);
}

void CodeHighlighter::highlightYaml(const QString &text)
{
    if (text.trimmed().startsWith('#')) {
        setFormat(0, text.size(), m_comment);
        return;
    }
    applyPattern(text, R"(^\s*(?:-\s+)?([\w.\-"' ]+?)\s*:(\s|$))", m_key, 1);
    applyPattern(text, R"(^\s*(-)\s)", m_keyword, 1);
    applyPattern(text, R"(:\s+(-?\d[\d._]*)\s*$)", m_number, 1);
    applyPattern(text, R"(\b(true|false|yes|no|null|~)\b)", m_type);
    applyPattern(text, R"("[^"]*"|'[^']*')", m_string);
    applyPattern(text, R"(^---$|^\.\.\.$)", m_preprocessor);
    applyPattern(text, R"([&*][\w-]+)", m_variable);
    applyPattern(text, R"(\s#.*$)", m_comment);
}
