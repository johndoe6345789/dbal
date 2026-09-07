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

// ===== Leftover words =====
//
// Every form stopped parsing once it had what it needed and ignored the
// rest, so "publish this at /about now" published to /about and dropped
// the word -- a typo anywhere past the understood part went unreported.

TEST(BqlParser, RefusesWordsAfterAPublishPath) {
    auto result = parseSentence("publish this at /about extra words");
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error.find("extra"), std::string::npos);
}

TEST(BqlParser, RefusesASecondPathTackedOn) {
    EXPECT_FALSE(parseSentence("publish this as \"A\" at /a and also at /b").ok);
}

TEST(BqlParser, RefusesWordsAfterStartANewPage) {
    EXPECT_FALSE(parseSentence("start a new page please").ok);
}

TEST(BqlParser, RefusesWordsAfterApply) {
    EXPECT_FALSE(parseSentence("apply \"x\" to hero now").ok);
}

TEST(BqlParser, StillAcceptsEveryWellFormedSentence) {
    EXPECT_TRUE(parseSentence("add a Container.").ok);
    EXPECT_TRUE(parseSentence("apply \"a\", \"b\" to hero").ok);
    EXPECT_TRUE(
        parseSentence("add a Container called row with direction of Across the page, gap of 24").ok);
}

// ===== Starting a fresh page =====
//
// Sentences add to whatever tree the editor has loaded, so a script for a
// second page silently inherited the first page's blocks, and running the
// same script twice added its blocks twice.

TEST(BqlParser, ParsesStartANewPage) {
    auto result = parseSentence("start a new page");
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.sentence.kind, BqlSentence::Kind::Clear);
}

TEST(BqlParser, StartANewPageTakesAnOptionalArticle) {
    EXPECT_TRUE(parseSentence("start new page").ok);
}

TEST(BqlParser, RefusesAHalfWrittenStartSentence) {
    EXPECT_FALSE(parseSentence("start a page").ok);
}

TEST(BqlParserJson, ClearSentenceRoundTripsExpectedShape) {
    auto result = parseSentence("start a new page");
    ASSERT_TRUE(result.ok);
    EXPECT_EQ(toJson(result.sentence)["kind"], "clear");
}

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

/**
 * A script builds either a page or a workflow. The workflow half exists
 * because dragging boxes on a canvas is the slowest way to say "when a
 * form arrives, write a line to the log" -- and the sentences are the
 * same shape either way, so nobody has to learn a second language.
 */
TEST(BqlParser, StartsAWorkflowByName) {
    const auto r = parseSentence(R"(start a new workflow called "Log a repair booking")");

    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(r.sentence.kind, BqlSentence::Kind::Workflow);
    EXPECT_EQ(r.sentence.name, "Log a repair booking");
}

TEST(BqlParser, AWorkflowHasToBeNamed) {
    const auto r = parseSentence("start a new workflow");

    EXPECT_FALSE(r.ok);
    EXPECT_NE(r.error.find("called"), std::string::npos);
}

// `start a new page` still means what it did.
TEST(BqlParser, StartingAPageIsUnaffected) {
    const auto r = parseSentence("start a new page");

    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(r.sentence.kind, BqlSentence::Kind::Clear);
}

TEST(BqlParser, ReadsWhatSetsAWorkflowGoing) {
    for (const char* line : {"run it when someone submits a form",
                             "run when a form is submitted"}) {
        const auto r = parseSentence(line);
        ASSERT_TRUE(r.ok) << line << ": " << r.error;
        EXPECT_EQ(r.sentence.kind, BqlSentence::Kind::Trigger);
        EXPECT_EQ(r.sentence.event, "FormSubmission.created");
    }
}

TEST(BqlParser, ReadsJoiningAsATrigger) {
    const auto r = parseSentence("run it when someone joins");

    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(r.sentence.event, "User.created");
}

/**
 * A trigger nobody implements is refused by name rather than accepted and
 * dropped: a workflow that silently never runs is the worst outcome here.
 */
TEST(BqlParser, RefusesATriggerItDoesNotKnow) {
    const auto r = parseSentence("run it when the moon is full");

    EXPECT_FALSE(r.ok);
    EXPECT_NE(r.error.find("the moon is full"), std::string::npos);
}

TEST(BqlParser, ReadsAStepAndItsParameters) {
    const auto r = parseSentence(
        R"(then Write a note to the log with message of "Booked")");

    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(r.sentence.kind, BqlSentence::Kind::Step);
    EXPECT_EQ(r.sentence.stepName, "Write a note to the log");
    ASSERT_EQ(r.sentence.attrs.size(), 1u);
    EXPECT_EQ(r.sentence.attrs[0].key, "message");
    EXPECT_EQ(r.sentence.attrs[0].value, "Booked");
}

TEST(BqlParser, ReadsAStepWithSeveralParameters) {
    const auto r = parseSentence(
        R"(then Only carry on if with value of "${event.data.job}", is of "contains", other of "urgent")");

    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(r.sentence.stepName, "Only carry on if");
    ASSERT_EQ(r.sentence.attrs.size(), 3u);
    EXPECT_EQ(r.sentence.attrs[2].value, "urgent");
}

// Which step names are real is the client's business, the same way block
// names are -- this parser has never known what a Heading 1 is either.
TEST(BqlParser, DoesNotJudgeWhetherAStepExists) {
    const auto r = parseSentence("then Frobnicate the widget");

    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(r.sentence.stepName, "Frobnicate the widget");
}

TEST(BqlParser, RefusesAStepWithNoName) {
    EXPECT_FALSE(parseSentence("then").ok);
}

TEST(BqlParser, PublishesAWorkflowWithoutAPath) {
    const auto r = parseSentence("publish the workflow");

    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(r.sentence.kind, BqlSentence::Kind::PublishWorkflow);
}

// The page form takes a path and still does.
TEST(BqlParser, PublishingAPageIsUnaffected) {
    const auto r = parseSentence(R"(publish this as "About" at /about)");

    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(r.sentence.kind, BqlSentence::Kind::Publish);
    EXPECT_EQ(r.sentence.path, "/about");
}
