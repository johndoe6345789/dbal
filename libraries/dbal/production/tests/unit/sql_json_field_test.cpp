/**
 * @file sql_json_field_test.cpp
 * @brief A json column has to come back as the object it went in as.
 *
 * Writing dumps an object to text; reading never parsed it back, so every
 * json field surfaced as a string. The visible consequence was a workflow
 * reading ${event.data.name} off a form submission and getting nothing:
 * `data` was a string, so the dot-path had no object to walk into, and the
 * step silently used its default instead of the visitor's answer.
 */

#include <gtest/gtest.h>

#include "adapters/sql/sql_result_parser.hpp"

using dbal::adapters::sql::SqlResultParser;
using dbal::core::EntityField;

namespace {

EntityField field(const std::string& type, bool required = true) {
    EntityField f;
    f.name     = "data";
    f.type     = type;
    f.required = required;
    return f;
}

} // namespace

TEST(SqlJsonField, RebuildsTheObjectThatWasStored) {
    const auto value = SqlResultParser::parseValue(
        R"({"name":"Rosa","job":"Buckled rear wheel"})", field("json"));

    ASSERT_TRUE(value.is_object());
    EXPECT_EQ(value["name"], "Rosa");
    EXPECT_EQ(value["job"], "Buckled rear wheel");
}

// ${event.data.name} is a dot-path, so `data` must be walkable.
TEST(SqlJsonField, LetsADotPathReachAField) {
    const auto value =
        SqlResultParser::parseValue(R"({"name":"Rosa"})", field("json"));

    ASSERT_TRUE(value.is_object());
    ASSERT_TRUE(value.contains("name"));
    EXPECT_EQ(value["name"].get<std::string>(), "Rosa");
}

TEST(SqlJsonField, RebuildsAnArrayToo) {
    const auto value =
        SqlResultParser::parseValue(R"(["a","b"])", field("json"));

    ASSERT_TRUE(value.is_array());
    EXPECT_EQ(value.size(), 2u);
}

TEST(SqlJsonField, KeepsAStringColumnAString) {
    const auto value =
        SqlResultParser::parseValue(R"({"a":1})", field("string"));

    ASSERT_TRUE(value.is_string());
    EXPECT_EQ(value.get<std::string>(), R"({"a":1})");
}

/**
 * Rows written before this parsed, or by anything that put plain text in
 * the column, must not vanish: text is more useful as itself than as null,
 * and it is at least visible to whoever has to fix it.
 */
TEST(SqlJsonField, FallsBackToTheTextWhenItWillNotParse) {
    const auto value = SqlResultParser::parseValue("not json", field("json"));

    ASSERT_TRUE(value.is_string());
    EXPECT_EQ(value.get<std::string>(), "not json");
}

TEST(SqlJsonField, LeavesOtherTypesAlone) {
    EXPECT_TRUE(SqlResultParser::parseValue("1", field("boolean")).get<bool>());
    EXPECT_EQ(SqlResultParser::parseValue("42", field("number")).get<int64_t>(),
              42);
}

TEST(SqlJsonField, AnEmptyOptionalColumnIsStillNull) {
    const auto value = SqlResultParser::parseValue("", field("json", false));
    EXPECT_TRUE(value.is_null());
}
