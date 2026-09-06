/**
 * @file sql_json_field_test.cpp
 * @brief A json column has to come back as the object it went in as.
 *
 * jsonValueToString writes an object with .dump(); rowToJson never parsed
 * it back, so every json field surfaced as text on the Postgres and MySQL
 * path (SQLite has the same fix in its own parser).
 *
 * The visible consequence was a workflow reading ${event.data.name} off a
 * form submission and getting nothing: `data` was a string, so the
 * dot-path had no object to walk into, and the step silently used its
 * default instead of the visitor's answer.
 */

#include <gtest/gtest.h>

#include "adapters/sql/sql_adapter_base.hpp"

using dbal::adapters::sql::SqlAdapter;

TEST(SqlJsonField, RebuildsTheObjectThatWasStored) {
    const nlohmann::json value = SqlAdapter::decodeJsonColumn(
        R"({"name":"Rosa","job":"Buckled rear wheel"})");

    ASSERT_TRUE(value.is_object());
    EXPECT_EQ(value["name"], "Rosa");
    EXPECT_EQ(value["job"], "Buckled rear wheel");
}

// ${event.data.name} is a dot-path, so `data` must be walkable.
TEST(SqlJsonField, LetsADotPathReachAField) {
    const nlohmann::json value =
        SqlAdapter::decodeJsonColumn(R"({"name":"Rosa"})");

    ASSERT_TRUE(value.is_object());
    ASSERT_TRUE(value.contains("name"));
    EXPECT_EQ(value["name"].get<std::string>(), "Rosa");
}

TEST(SqlJsonField, RebuildsANestedObject) {
    const nlohmann::json value =
        SqlAdapter::decodeJsonColumn(R"({"a":{"b":"c"}})");

    ASSERT_TRUE(value.is_object());
    EXPECT_EQ(value["a"]["b"], "c");
}

TEST(SqlJsonField, RebuildsAnArrayToo) {
    const nlohmann::json value = SqlAdapter::decodeJsonColumn(R"(["a","b"])");

    ASSERT_TRUE(value.is_array());
    EXPECT_EQ(value.size(), 2u);
}

/**
 * Rows written before this parsed, or by anything that put plain text in
 * the column, must not vanish: text is more useful as itself than as null,
 * and it is at least visible to whoever has to fix it.
 */
TEST(SqlJsonField, FallsBackToTheTextWhenItWillNotParse) {
    const nlohmann::json value = SqlAdapter::decodeJsonColumn("not json");

    ASSERT_TRUE(value.is_string());
    EXPECT_EQ(value.get<std::string>(), "not json");
}

// A bare word is not JSON; a bare number is. Neither may throw.
TEST(SqlJsonField, SurvivesOddButHarmlessText) {
    EXPECT_TRUE(SqlAdapter::decodeJsonColumn("42").is_number());
    EXPECT_TRUE(SqlAdapter::decodeJsonColumn("true").is_boolean());
    EXPECT_TRUE(SqlAdapter::decodeJsonColumn("{oops").is_string());
}
