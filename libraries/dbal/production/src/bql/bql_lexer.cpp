#include "bql_lexer.hpp"
#include <cctype>

namespace dbal {
namespace bql {

namespace {
bool isDigitChar(char c) { return c >= '0' && c <= '9'; }
}

std::vector<Token> tokenize(const std::string& line) {
    std::vector<Token> tokens;
    const size_t n = line.size();
    size_t i = 0;
    while (i < n) {
        char ch = line[i];
        if (std::isspace(static_cast<unsigned char>(ch)) != 0) {
            i += 1;
            continue;
        }
        if (ch == '"') {
            size_t end = line.find('"', i + 1);
            size_t close = (end == std::string::npos) ? n : end;
            tokens.push_back({Token::Type::String, line.substr(i + 1, close - (i + 1))});
            i = close + 1;
            continue;
        }
        if (ch == ',') {
            tokens.push_back({Token::Type::Punct, ","});
            i += 1;
            continue;
        }
        if (ch == '.' && !(i + 1 < n && isDigitChar(line[i + 1]))) {
            tokens.push_back({Token::Type::Punct, "."});
            i += 1;
            continue;
        }
        size_t j = i;
        while (j < n) {
            char next = line[j];
            if (std::isspace(static_cast<unsigned char>(next)) != 0 || next == '"' || next == ',') {
                break;
            }
            if (next == '.' && !(j + 1 < n && isDigitChar(line[j + 1]))) break;
            j += 1;
        }
        tokens.push_back({Token::Type::Word, line.substr(i, j - i)});
        i = j;
    }
    return tokens;
}

} // namespace bql
} // namespace dbal
