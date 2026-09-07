#pragma once
#include <nlohmann/json.hpp>
#include <string>

namespace dbal::workflow {

/**
 * Whether @p value stands in relation @p is to @p other.
 *
 * Deliberately small and word-shaped -- "equals", "contains", "empty",
 * "not empty" -- because the people writing these are describing what
 * their business does, not writing an expression language. An unknown
 * relation is false rather than an error: a workflow that quietly does
 * nothing is easier to diagnose than one that stops with a parse error
 * nobody sees.
 */
bool compareHolds(const std::string& is,
                  const nlohmann::json& value,
                  const nlohmann::json& other);

/** A json value as the text a comparison should see. */
std::string asText(const nlohmann::json& value);

} // namespace dbal::workflow
