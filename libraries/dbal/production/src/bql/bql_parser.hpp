#pragma once

#include <optional>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "bql_lexer.hpp"

namespace dbal {
namespace bql {

/**
 * The five BQL sentence forms, as pure syntax -- this layer knows nothing
 * about what block names or properties are valid (that's the calling app's
 * job, against its own vocabulary). See
 * frontends/nextjs/.../bql/parser.ts for the reference grammar this is a
 * line-for-line C++ port of; keep both in sync, since this is the version
 * meant to be shared across apps rather than reimplemented per app.
 */
struct BqlAttr {
    std::string key;
    std::string value;
};

struct BqlSentence {
    enum class Kind { Add, Give, Style, Class };
    Kind kind = Kind::Add;
    int line = 0;  // 1-based source line, set by parseScript

    std::string blockName;                   // add
    std::optional<std::string> text;         // add
    std::optional<std::string> alias;        // add (own alias), give/class (target)
    std::optional<std::string> parentAlias;  // add
    std::vector<BqlAttr> attrs;              // add, give, style

    std::string name;                        // style (the class name)
    std::vector<std::string> names;          // class (the class names applied)
};

struct SentenceResult {
    bool ok = false;
    BqlSentence sentence;
    std::string error;
};

SentenceResult parseSentence(const std::string& rawLine);

/** Same shape as the TS `BqlSentence` union, so a client can consume this
 *  response exactly as it would the local parser's output. */
nlohmann::json toJson(const BqlSentence& sentence);

struct ScriptError {
    int line = 0;
    std::string message;
};

struct ScriptParseResult {
    bool ok = false;
    std::vector<BqlSentence> sentences;
    std::vector<ScriptError> errors;
};

/** Parses every non-blank, non-"#comment" line of a script. Blank lines and
 *  the syntax error contract mirror apply.ts's parseAllSentences: every line
 *  is checked before any result is returned, so a caller either gets every
 *  sentence or every error, never a partial list to guess from. */
ScriptParseResult parseScript(const std::string& script);

nlohmann::json toJson(const ScriptParseResult& result);

} // namespace bql
} // namespace dbal
