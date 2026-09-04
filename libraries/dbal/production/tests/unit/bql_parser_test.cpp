/**
 * @file bql_parser_test.cpp
 * @brief Unit tests for the BQL lexer/parser -- mirrors the reference test
 *        cases in frontends/nextjs/.../bql/{lexer,parser,apply}.test.ts so
 *        both implementations are held to the same behavior.
 */
#include <gtest/gtest.h>
#include "bql/bql_lexer.hpp"
#include "bql/bql_parser.hpp"

using namespace dbal::bql;

// ===== Lexer =====

TEST(BqlLexer, SplitsWordsAndTrailingPeriod) {
    auto tokens = tokenize("Add a Container.");
    std::vector<Token> expected{
        {Token::Type::Word, "Add"}, {Token::Type::Word, "a"},
        {Token::Type::Word, "Container"}, {Token::Type::Punct, "."},
    };
    EXPECT_EQ(tokens, expected);
}

TEST(BqlLexer, KeepsQuotedStringWhole) {
    auto tokens = tokenize("that says \"Trade prints, and enjoy it.\"");
    std::vector<Token> expected{
        {Token::Type::Word, "that"}, {Token::Type::Word, "says"},
        {Token::Type::String, "Trade prints, and enjoy it."},
    };
    EXPECT_EQ(tokens, expected);
}

TEST(BqlLexer, KeepsHashedWordIntact) {
    auto tokens = tokenize("a background of #1a1a1a");
    std::vector<Token> expected{
        {Token::Type::Word, "a"}, {Token::Type::Word, "background"},
        {Token::Type::Word, "of"}, {Token::Type::Word, "#1a1a1a"},
    };
    EXPECT_EQ(tokens, expected);
}

TEST(BqlLexer, EmitsCommaAsOwnToken) {
    auto tokens = tokenize("Inside hero, add a Box");
    std::vector<Token> expected{
        {Token::Type::Word, "Inside"}, {Token::Type::Word, "hero"},
        {Token::Type::Punct, ","}, {Token::Type::Word, "add"},
        {Token::Type::Word, "a"}, {Token::Type::Word, "Box"},
    };
    EXPECT_EQ(tokens, expected);
}

TEST(BqlLexer, DoesNotBreakOnDecimalPoint) {
    auto tokens = tokenize("a value of 3.5");
    EXPECT_EQ(tokens.back(), (Token{Token::Type::Word, "3.5"}));
}

// ===== Parser: ADD =====

TEST(BqlParser, ReadsABareBlockName) {
    auto r = parseSentence("Add a Container.");
    ASSERT_TRUE(r.ok);
    EXPECT_EQ(r.sentence.kind, BqlSentence::Kind::Add);
    EXPECT_EQ(r.sentence.blockName, "Container");
    EXPECT_TRUE(r.sentence.attrs.empty());
}

TEST(BqlParser, ReadsContentAliasAndPropertiesTogether) {
    auto r = parseSentence(
        "Add a Button called heroCta that says \"Join now\" with a style of Solid.");
    ASSERT_TRUE(r.ok);
    EXPECT_EQ(r.sentence.blockName, "Button");
    EXPECT_EQ(r.sentence.alias, "heroCta");
    EXPECT_EQ(r.sentence.text, "Join now");
    ASSERT_EQ(r.sentence.attrs.size(), 1u);
    EXPECT_EQ(r.sentence.attrs[0].key, "style");
    EXPECT_EQ(r.sentence.attrs[0].value, "Solid");
}

TEST(BqlParser, QuotedContentIsNeverMistakenForAClauseKeyword) {
    auto r = parseSentence(
        "Add a Paragraph that says \"Trade prints with other members, and enjoy it.\"");
    ASSERT_TRUE(r.ok);
    EXPECT_EQ(r.sentence.blockName, "Paragraph");
    EXPECT_EQ(r.sentence.text, "Trade prints with other members, and enjoy it.");
    EXPECT_TRUE(r.sentence.attrs.empty());
}

TEST(BqlParser, AcceptsAnBeforeAVowel) {
    auto r = parseSentence("Add an Alert that says \"Hello\".");
    ASSERT_TRUE(r.ok);
    EXPECT_EQ(r.sentence.blockName, "Alert");
    EXPECT_EQ(r.sentence.text, "Hello");
}

// ===== Parser: INSIDE ... ADD =====

TEST(BqlParser, InsideAddRecordsParentAlias) {
    auto r = parseSentence("Inside hero, add a Heading 1 that says \"Community Darkroom\".");
    ASSERT_TRUE(r.ok);
    EXPECT_EQ(r.sentence.parentAlias, "hero");
    EXPECT_EQ(r.sentence.blockName, "Heading 1");
    EXPECT_EQ(r.sentence.text, "Community Darkroom");
}

// ===== Parser: GIVE =====

TEST(BqlParser, GiveReadsTargetAndProperties) {
    auto r = parseSentence("Give heroCta a style of Solid.");
    ASSERT_TRUE(r.ok);
    EXPECT_EQ(r.sentence.kind, BqlSentence::Kind::Give);
    EXPECT_EQ(r.sentence.alias, "heroCta");
    ASSERT_EQ(r.sentence.attrs.size(), 1u);
    EXPECT_EQ(r.sentence.attrs[0].key, "style");
    EXPECT_EQ(r.sentence.attrs[0].value, "Solid");
}

// ===== Parser: STYLE =====

TEST(BqlParser, StyleReadsNameAndDeclarations) {
    auto r = parseSentence(
        "Make a style called \"hero-panel\" with a background of #1a1a1a and a padding of 32.");
    ASSERT_TRUE(r.ok);
    EXPECT_EQ(r.sentence.kind, BqlSentence::Kind::Style);
    EXPECT_EQ(r.sentence.name, "hero-panel");
    ASSERT_EQ(r.sentence.attrs.size(), 2u);
    EXPECT_EQ(r.sentence.attrs[0].key, "background");
    EXPECT_EQ(r.sentence.attrs[0].value, "#1a1a1a");
    EXPECT_EQ(r.sentence.attrs[1].key, "padding");
    EXPECT_EQ(r.sentence.attrs[1].value, "32");
}

TEST(BqlParser, StyleWithNoDeclarationsYet) {
    auto r = parseSentence("Make a style called \"hero-panel\".");
    ASSERT_TRUE(r.ok);
    EXPECT_TRUE(r.sentence.attrs.empty());
}

// ===== Parser: APPLY =====

TEST(BqlParser, ApplyReadsOneClassName) {
    auto r = parseSentence("Apply \"hero-panel\" to hero.");
    ASSERT_TRUE(r.ok);
    EXPECT_EQ(r.sentence.kind, BqlSentence::Kind::Class);
    ASSERT_EQ(r.sentence.names.size(), 1u);
    EXPECT_EQ(r.sentence.names[0], "hero-panel");
    EXPECT_EQ(r.sentence.alias, "hero");
}

TEST(BqlParser, ApplyReadsSeveralClassNames) {
    auto r = parseSentence("Apply \"hero-panel\" and \"shadow\" to hero.");
    ASSERT_TRUE(r.ok);
    ASSERT_EQ(r.sentence.names.size(), 2u);
    EXPECT_EQ(r.sentence.names[0], "hero-panel");
    EXPECT_EQ(r.sentence.names[1], "shadow");
}

// ===== Parser: multi-word attribute values =====

TEST(BqlParser, AttributeValueCanBeSeveralWords) {
    auto r = parseSentence(
        "Add a Container called cardRow with a direction of Across the page and a gap of 24.");
    ASSERT_TRUE(r.ok);
    ASSERT_EQ(r.sentence.attrs.size(), 2u);
    EXPECT_EQ(r.sentence.attrs[0].key, "direction");
    EXPECT_EQ(r.sentence.attrs[0].value, "Across the page");
    EXPECT_EQ(r.sentence.attrs[1].key, "gap");
    EXPECT_EQ(r.sentence.attrs[1].value, "24");
}

// ===== Parser: unrecognized input =====

TEST(BqlParser, ReportsAnErrorInsteadOfGuessing) {
    auto r = parseSentence("Please make it nicer.");
    EXPECT_FALSE(r.ok);
    EXPECT_FALSE(r.error.empty());
}

// ===== JSON serialization =====

// ===== Publishing a page =====
//
// A script that builds a page could not say where the page goes, so the
// route had to be set by hand in the panel between running the script and
// pressing Publish -- and the path silently reverted if you changed tabs.

TEST(BqlParser, ParsesPublishWithATitle) {
    auto result = parseSentence("publish this as \"About\" at /about");
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.sentence.kind, BqlSentence::Kind::Publish);
    EXPECT_EQ(result.sentence.name, "About");
    EXPECT_EQ(result.sentence.path, "/about");
}

TEST(BqlParser, ParsesPublishWithoutATitle) {
    auto result = parseSentence("publish this at /contact");
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.sentence.kind, BqlSentence::Kind::Publish);
    EXPECT_TRUE(result.sentence.name.empty());
    EXPECT_EQ(result.sentence.path, "/contact");
}

TEST(BqlParser, ParsesPublishWithoutTheWordThis) {
    auto result = parseSentence("publish as \"Home\" at /");
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.sentence.name, "Home");
    EXPECT_EQ(result.sentence.path, "/");
}

TEST(BqlParser, AcceptsAQuotedPathToo) {
    auto result = parseSentence("publish this as \"About\" at \"/about\"");
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.sentence.path, "/about");
}

TEST(BqlParser, RefusesPublishWithNoPath) {
    auto result = parseSentence("publish this as \"About\"");
    EXPECT_FALSE(result.ok);
}

TEST(BqlParserJson, PublishSentenceRoundTripsExpectedShape) {
    auto result = parseSentence("publish this as \"About\" at /about");
    ASSERT_TRUE(result.ok);
    auto json = toJson(result.sentence);
    EXPECT_EQ(json["kind"], "publish");
    EXPECT_EQ(json["title"], "About");
    EXPECT_EQ(json["path"], "/about");
}

TEST(BqlParserJson, PublishWithoutATitleOmitsIt) {
    auto result = parseSentence("publish this at /about");
    ASSERT_TRUE(result.ok);
    auto json = toJson(result.sentence);
    EXPECT_FALSE(json.contains("title"));
    EXPECT_EQ(json["path"], "/about");
}

TEST(BqlParserJson, AddSentenceRoundTripsExpectedShape) {
    auto r = parseSentence("Inside hero, add a Button called heroCta that says \"Join now\".");
    ASSERT_TRUE(r.ok);
    auto j = toJson(r.sentence);
    EXPECT_EQ(j.at("kind"), "add");
    EXPECT_EQ(j.at("blockName"), "Button");
    EXPECT_EQ(j.at("alias"), "heroCta");
    EXPECT_EQ(j.at("parentAlias"), "hero");
    EXPECT_EQ(j.at("text"), "Join now");
}

// ===== Whole script: fail-closed semantics =====

TEST(BqlScript, ParsesEveryLineSkippingBlanksAndComments) {
    auto r = parseScript(
        "# a comment\n"
        "\n"
        "Add a Container called hero.\n"
        "Inside hero, add a Heading 1 that says \"Hi\".\n");
    EXPECT_TRUE(r.ok);
    ASSERT_EQ(r.sentences.size(), 2u);
    EXPECT_EQ(r.sentences[0].blockName, "Container");
    EXPECT_EQ(r.sentences[1].parentAlias, "hero");
}

TEST(BqlScript, CollectsEveryErrorRatherThanStoppingAtTheFirst) {
    auto r = parseScript(
        "Add a Container called hero.\n"
        "Please make it nicer.\n"
        "Also not a sentence.\n");
    EXPECT_FALSE(r.ok);
    EXPECT_TRUE(r.sentences.empty());
    ASSERT_EQ(r.errors.size(), 2u);
    EXPECT_EQ(r.errors[0].line, 2);
    EXPECT_EQ(r.errors[1].line, 3);
}

TEST(BqlScript, StampsEachSentenceWithItsSourceLine) {
    auto r = parseScript(
        "# a comment\n"
        "\n"
        "Add a Container called hero.\n"
        "Inside hero, add a Heading 1 that says \"Hi\".\n");
    ASSERT_TRUE(r.ok);
    ASSERT_EQ(r.sentences.size(), 2u);
    EXPECT_EQ(r.sentences[0].line, 3);
    EXPECT_EQ(r.sentences[1].line, 4);
    EXPECT_EQ(toJson(r.sentences[0]).at("line"), 3);
}

TEST(BqlScript, BuildsTheWholeCommunityDarkroomHomepageInOneScript) {
    auto r = parseScript(
        "Add a Container called hero with a gap of 16.\n"
        "Inside hero, add a Heading 1 that says \"Community Darkroom\".\n"
        "Inside hero, add a Button called heroCta that says \"Join now\".\n"
        "Give heroCta a style of Solid.\n"
        "\n"
        "Add a Container called cardRow with a direction of Across the page and a gap of 24.\n"
        "Inside cardRow, add a Container called card1 with a gap of 8.\n"
        "Inside card1, add a Heading 3 that says \"Community darkrooms\".\n"
        "\n"
        "Add an Alert that says \"New: weekend darkroom slots just opened up.\" with a kind of Information.\n"
        "\n"
        "Make a style called \"hero-panel\" with a background of #1a1a1a and a padding of 32.\n"
        "Apply \"hero-panel\" to hero.\n");
    ASSERT_TRUE(r.ok);
    EXPECT_EQ(r.sentences.size(), 10u);
    EXPECT_EQ(r.sentences.back().kind, BqlSentence::Kind::Class);
}
