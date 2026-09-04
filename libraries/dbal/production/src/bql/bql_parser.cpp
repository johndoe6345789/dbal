#include "bql_parser.hpp"
#include <algorithm>
#include <cctype>

namespace dbal {
namespace bql {

namespace {

std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

/** Walks a token list with lookahead, the way a real parser would, instead
 *  of peeling substrings with regexes. */
class Cursor {
public:
    explicit Cursor(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

    const Token* peek(size_t offset = 0) const {
        size_t i = pos_ + offset;
        return i < tokens_.size() ? &tokens_[i] : nullptr;
    }

    const Token* next() {
        const Token* t = peek();
        pos_ += 1;
        return t;
    }

private:
    std::vector<Token> tokens_;
    size_t pos_ = 0;
};

bool isWord(const Cursor& c, const std::string& value, size_t offset = 0) {
    const Token* t = c.peek(offset);
    return t != nullptr && t->type == Token::Type::Word && toLower(t->value) == value;
}

bool isComma(const Cursor& c) {
    const Token* t = c.peek();
    return t != nullptr && t->type == Token::Type::Punct && t->value == ",";
}

void skipArticle(Cursor& c) {
    if (isWord(c, "a") || isWord(c, "an")) c.next();
}

/** Consumes bare words up to (not including) the next `stopWords` keyword
 *  or any non-word token -- "everything not otherwise claimed" is how a
 *  block name, and an attribute's key, are both defined. */
std::string consumeWordsUntil(Cursor& c, const std::vector<std::string>& stopWords) {
    std::vector<std::string> words;
    for (const Token* t = c.peek(); t != nullptr && t->type == Token::Type::Word; t = c.peek()) {
        std::string lower = toLower(t->value);
        if (std::find(stopWords.begin(), stopWords.end(), lower) != stopWords.end()) break;
        words.push_back(t->value);
        c.next();
    }
    std::string joined;
    for (size_t i = 0; i < words.size(); ++i) {
        if (i > 0) joined += " ";
        joined += words[i];
    }
    return joined;
}

/** A comma, "and", or ", and" between two items in a `with ... and ...`
 *  list -- consumes it and reports whether one was there. */
bool consumeSeparator(Cursor& c) {
    bool found = false;
    if (isComma(c)) {
        c.next();
        found = true;
    }
    if (isWord(c, "and")) {
        c.next();
        found = true;
    }
    return found;
}

std::optional<std::string> parseValue(Cursor& c) {
    const Token* t = c.peek();
    if (t == nullptr) return std::nullopt;
    if (t->type == Token::Type::String) {
        c.next();
        return t->value;
    }
    std::vector<std::string> words;
    for (const Token* next = c.peek();
         next != nullptr && next->type == Token::Type::Word && toLower(next->value) != "and";
         next = c.peek()) {
        words.push_back(next->value);
        c.next();
    }
    if (words.empty()) return std::nullopt;
    std::string joined;
    for (size_t i = 0; i < words.size(); ++i) {
        if (i > 0) joined += " ";
        joined += words[i];
    }
    return joined;
}

std::optional<BqlAttr> parseAttrItem(Cursor& c) {
    skipArticle(c);
    std::string key = consumeWordsUntil(c, {"of"});
    if (key.empty() || !isWord(c, "of")) return std::nullopt;
    c.next();
    auto value = parseValue(c);
    if (!value.has_value()) return std::nullopt;
    return BqlAttr{key, *value};
}

std::optional<std::vector<BqlAttr>> parseAttrList(Cursor& c) {
    auto first = parseAttrItem(c);
    if (!first.has_value()) return std::nullopt;
    std::vector<BqlAttr> attrs{*first};
    while (consumeSeparator(c)) {
        auto next = parseAttrItem(c);
        if (!next.has_value()) return std::nullopt;
        attrs.push_back(*next);
    }
    return attrs;
}

struct AddClause {
    std::string blockName;
    std::optional<std::string> alias;
    std::optional<std::string> text;
    std::vector<BqlAttr> attrs;
};

std::optional<AddClause> parseAddClause(Cursor& c) {
    skipArticle(c);
    std::string blockName = consumeWordsUntil(c, {"called", "that", "with"});
    if (blockName.empty()) return std::nullopt;

    std::optional<std::string> alias;
    if (isWord(c, "called")) {
        c.next();
        const Token* t = c.next();
        if (t == nullptr || t->type != Token::Type::Word) return std::nullopt;
        alias = t->value;
    }

    std::optional<std::string> text;
    if (isWord(c, "that")) {
        c.next();
        if (!isWord(c, "says")) return std::nullopt;
        c.next();
        const Token* t = c.next();
        if (t == nullptr || t->type != Token::Type::String) return std::nullopt;
        text = t->value;
    }

    std::vector<BqlAttr> attrs;
    if (isWord(c, "with")) {
        c.next();
        auto parsed = parseAttrList(c);
        if (!parsed.has_value()) return std::nullopt;
        attrs = *parsed;
    }

    return AddClause{blockName, alias, text, attrs};
}

std::vector<std::string> parseNameList(Cursor& c) {
    std::vector<std::string> names;
    for (const Token* t = c.peek(); t != nullptr && t->type == Token::Type::String; t = c.peek()) {
        names.push_back(t->value);
        c.next();
        if (!consumeSeparator(c)) break;
    }
    return names;
}

SentenceResult fail(const std::string& message) {
    SentenceResult r;
    r.ok = false;
    r.error = message;
    return r;
}

SentenceResult addResult(const AddClause& clause, std::optional<std::string> parentAlias) {
    SentenceResult r;
    r.ok = true;
    r.sentence.kind = BqlSentence::Kind::Add;
    r.sentence.blockName = clause.blockName;
    r.sentence.alias = clause.alias;
    r.sentence.text = clause.text;
    r.sentence.attrs = clause.attrs;
    r.sentence.parentAlias = std::move(parentAlias);
    return r;
}

SentenceResult parseAdd(Cursor& c, const std::string& rawLine) {
    c.next();
    auto clause = parseAddClause(c);
    if (!clause.has_value()) return fail("Could not read the block in: \"" + rawLine + "\"");
    return addResult(*clause, std::nullopt);
}

SentenceResult parseInsideAdd(Cursor& c, const std::string& rawLine) {
    c.next();
    const Token* parentTok = c.next();
    if (parentTok == nullptr || parentTok->type != Token::Type::Word) {
        return fail("Could not read: \"" + rawLine + "\"");
    }
    if (isComma(c)) c.next();
    if (!isWord(c, "add")) return fail("Could not read: \"" + rawLine + "\"");
    c.next();
    auto clause = parseAddClause(c);
    if (!clause.has_value()) return fail("Could not read the block in: \"" + rawLine + "\"");
    return addResult(*clause, parentTok->value);
}

SentenceResult parseGive(Cursor& c, const std::string& rawLine) {
    c.next();
    const Token* aliasTok = c.next();
    if (aliasTok == nullptr || aliasTok->type != Token::Type::Word) {
        return fail("Could not read: \"" + rawLine + "\"");
    }
    auto attrs = parseAttrList(c);
    if (!attrs.has_value()) {
        return fail("Could not read the properties in: \"" + rawLine + "\"");
    }
    SentenceResult r;
    r.ok = true;
    r.sentence.kind = BqlSentence::Kind::Give;
    r.sentence.alias = aliasTok->value;
    r.sentence.attrs = *attrs;
    return r;
}

SentenceResult parseStyle(Cursor& c, const std::string& rawLine) {
    c.next();
    skipArticle(c);
    if (!isWord(c, "style") || !isWord(c, "called", 1)) {
        return fail("Could not read: \"" + rawLine + "\"");
    }
    c.next();
    c.next();
    const Token* nameTok = c.next();
    if (nameTok == nullptr || nameTok->type != Token::Type::String) {
        return fail("Could not read: \"" + rawLine + "\"");
    }
    std::vector<BqlAttr> attrs;
    if (isWord(c, "with")) {
        c.next();
        auto parsed = parseAttrList(c);
        if (!parsed.has_value()) {
            return fail("Could not read the properties in: \"" + rawLine + "\"");
        }
        attrs = *parsed;
    }
    SentenceResult r;
    r.ok = true;
    r.sentence.kind = BqlSentence::Kind::Style;
    r.sentence.name = nameTok->value;
    r.sentence.attrs = attrs;
    return r;
}

SentenceResult parseApply(Cursor& c, const std::string& rawLine) {
    c.next();
    auto names = parseNameList(c);
    if (names.empty()) return fail("Nothing to apply in: \"" + rawLine + "\"");
    if (!isWord(c, "to")) return fail("Could not read: \"" + rawLine + "\"");
    c.next();
    const Token* aliasTok = c.next();
    if (aliasTok == nullptr || aliasTok->type != Token::Type::Word) {
        return fail("Could not read: \"" + rawLine + "\"");
    }
    SentenceResult r;
    r.ok = true;
    r.sentence.kind = BqlSentence::Kind::Class;
    r.sentence.names = names;
    r.sentence.alias = aliasTok->value;
    return r;
}


/** `publish this as "About" at /about` -- says where the tree the script
 *  just built should live. The title is optional (a caller can fall back to
 *  the path), and the path may be bare or quoted: the lexer treats /about as
 *  one word, so quoting it is a preference rather than a requirement. */
SentenceResult parsePublish(Cursor& c, const std::string& rawLine) {
    c.next();
    if (isWord(c, "this")) c.next();

    std::string title;
    if (isWord(c, "as")) {
        c.next();
        const Token* titleTok = c.next();
        if (titleTok == nullptr || titleTok->type != Token::Type::String) {
            return fail("Could not read the title in: \"" + rawLine + "\"");
        }
        title = titleTok->value;
    }

    if (!isWord(c, "at")) {
        return fail("Missing \"at <path>\" in: \"" + rawLine + "\"");
    }
    c.next();
    const Token* pathTok = c.next();
    if (pathTok == nullptr ||
        (pathTok->type != Token::Type::Word && pathTok->type != Token::Type::String)) {
        return fail("Could not read the path in: \"" + rawLine + "\"");
    }

    SentenceResult r;
    r.ok = true;
    r.sentence.kind = BqlSentence::Kind::Publish;
    r.sentence.name = title;
    r.sentence.path = pathTok->value;
    return r;
}


/** `start a new page` -- everything after this line builds a page of its
 *  own rather than adding to whatever the editor already had loaded.
 *  Without it a script that ends in `publish this at /classes` quietly
 *  appended its blocks to the previous page still in the editor, and a
 *  re-run appended them a second time. */
SentenceResult parseClear(Cursor& c, const std::string& rawLine) {
    c.next();
    skipArticle(c);
    if (!isWord(c, "new")) return fail("Could not read: \"" + rawLine + "\"");
    c.next();
    if (!isWord(c, "page")) return fail("Could not read: \"" + rawLine + "\"");
    c.next();

    SentenceResult r;
    r.ok = true;
    r.sentence.kind = BqlSentence::Kind::Clear;
    return r;
}

std::vector<Token> stripTerminator(std::vector<Token> tokens) {
    if (!tokens.empty()) {
        const Token& last = tokens.back();
        if (last.type == Token::Type::Punct && last.value == ".") tokens.pop_back();
    }
    return tokens;
}

} // namespace

SentenceResult parseSentence(const std::string& rawLine) {
    Cursor c(stripTerminator(tokenize(trim(rawLine))));
    if (isWord(c, "start")) return parseClear(c, rawLine);
    if (isWord(c, "publish")) return parsePublish(c, rawLine);
    if (isWord(c, "apply")) return parseApply(c, rawLine);
    if (isWord(c, "make")) return parseStyle(c, rawLine);
    if (isWord(c, "give")) return parseGive(c, rawLine);
    if (isWord(c, "inside")) return parseInsideAdd(c, rawLine);
    if (isWord(c, "add")) return parseAdd(c, rawLine);
    return fail("Didn't understand: \"" + rawLine + "\"");
}

ScriptParseResult parseScript(const std::string& script) {
    ScriptParseResult result;
    result.ok = true;

    std::vector<std::string> lines;
    size_t start = 0;
    while (start <= script.size()) {
        size_t end = script.find('\n', start);
        if (end == std::string::npos) {
            lines.push_back(script.substr(start));
            break;
        }
        lines.push_back(script.substr(start, end - start));
        start = end + 1;
    }

    for (size_t i = 0; i < lines.size(); ++i) {
        std::string text = trim(lines[i]);
        if (text.empty() || text.front() == '#') continue;
        auto parsed = parseSentence(text);
        if (parsed.ok) {
            parsed.sentence.line = static_cast<int>(i) + 1;
            result.sentences.push_back(parsed.sentence);
        } else {
            result.errors.push_back({static_cast<int>(i) + 1, parsed.error});
        }
    }

    result.ok = result.errors.empty();
    if (!result.ok) result.sentences.clear();
    return result;
}

nlohmann::json toJson(const ScriptParseResult& result) {
    nlohmann::json j;
    j["ok"] = result.ok;
    if (result.ok) {
        nlohmann::json sentences = nlohmann::json::array();
        for (const auto& s : result.sentences) sentences.push_back(toJson(s));
        j["sentences"] = sentences;
    } else {
        nlohmann::json errors = nlohmann::json::array();
        for (const auto& e : result.errors) {
            errors.push_back({{"line", e.line}, {"message", e.message}});
        }
        j["errors"] = errors;
    }
    return j;
}

nlohmann::json toJson(const BqlSentence& sentence) {
    nlohmann::json attrs = nlohmann::json::array();
    for (const auto& attr : sentence.attrs) {
        attrs.push_back({{"key", attr.key}, {"value", attr.value}});
    }

    nlohmann::json j;
    switch (sentence.kind) {
        case BqlSentence::Kind::Add: {
            j = {{"kind", "add"}, {"blockName", sentence.blockName}, {"attrs", attrs}};
            if (sentence.text.has_value()) j["text"] = *sentence.text;
            if (sentence.alias.has_value()) j["alias"] = *sentence.alias;
            if (sentence.parentAlias.has_value()) j["parentAlias"] = *sentence.parentAlias;
            break;
        }
        case BqlSentence::Kind::Give:
            j = {{"kind", "give"}, {"alias", *sentence.alias}, {"attrs", attrs}};
            break;
        case BqlSentence::Kind::Style:
            j = {{"kind", "style"}, {"name", sentence.name}, {"attrs", attrs}};
            break;
        case BqlSentence::Kind::Class:
            j = {{"kind", "class"}, {"names", sentence.names}, {"alias", *sentence.alias}};
            break;
        case BqlSentence::Kind::Clear:
            j = {{"kind", "clear"}};
            break;
        case BqlSentence::Kind::Publish: {
            j = {{"kind", "publish"}, {"path", sentence.path}};
            // Absent rather than empty: a caller falls back to the path, and
            // "" would read as a page deliberately titled nothing.
            if (!sentence.name.empty()) j["title"] = sentence.name;
            break;
        }
    }
    j["line"] = sentence.line;
    return j;
}

} // namespace bql
} // namespace dbal
