#pragma once

#include <string>
#include <vector>

namespace dbal {
namespace bql {

/**
 * Tokenizes one BQL line the way a real language's lexer would, rather than
 * peeling substrings with regexes. Three kinds: a quoted run of text (kept
 * whole, punctuation and all -- "with", "and", "." inside a quote is
 * content, never a clause boundary), a bare word (anything else with no
 * whitespace -- identifiers, numbers, hex colors; a multi-word value is
 * just several word tokens in a row), and punctuation (the "," and "."
 * that separate clauses and sentences).
 *
 * Ported from the reference implementation in
 * frontends/nextjs/.../bql/lexer.ts -- keep the two in sync; this is the
 * canonical parser other apps are meant to call instead of reimplementing.
 */
struct Token {
    enum class Type { Word, String, Punct };
    Type type;
    std::string value;

    bool operator==(const Token& other) const {
        return type == other.type && value == other.value;
    }
};

std::vector<Token> tokenize(const std::string& line);

} // namespace bql
} // namespace dbal
