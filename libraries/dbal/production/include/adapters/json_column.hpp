#pragma once
#include <nlohmann/json.hpp>
#include <string>

namespace dbal::adapters {

/**
 * A json column's text, back as the value it went in as.
 *
 * Writing dumps an object with .dump(); without the matching parse a json
 * column comes back as text. A workflow reading ${event.data.name} off a
 * form submission got nothing at all -- `data` was a string, so the
 * dot-path had no object to walk into, and the step quietly used its
 * default instead of the visitor's answer.
 *
 * Shared by the SQL and SQLite adapters, which each have their own row
 * parser and so each needed the same fix. One of them having it and the
 * other not is exactly the shape of bug worth removing the chance of.
 *
 * Text that will not parse stays itself: rows written before this, or by
 * anything that put plain text in the column, are more useful visible
 * than gone.
 */
nlohmann::json decodeJsonColumn(const std::string& value);

} // namespace dbal::adapters
