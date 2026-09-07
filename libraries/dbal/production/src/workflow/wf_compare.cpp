#include "workflow/wf_compare.hpp"
#include <algorithm>
#include <cctype>

namespace dbal::workflow {

namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return s;
}

} // namespace

std::string asText(const nlohmann::json& value) {
    if (value.is_null()) return {};
    if (value.is_string()) return value.get<std::string>();
    return value.dump();
}

bool compareHolds(const std::string& is,
                  const nlohmann::json& value,
                  const nlohmann::json& other) {
    const std::string got = asText(value);
    const std::string want = asText(other);
    const std::string rel = lower(is);

    if (rel == "empty") return got.empty();
    if (rel == "not empty") return !got.empty();
    if (rel == "equals") return got == want;
    if (rel == "does not equal") return got != want;
    if (rel == "contains") return got.find(want) != std::string::npos;
    if (rel == "does not contain") return got.find(want) == std::string::npos;
    if (rel == "is true") return value.is_boolean() ? value.get<bool>()
                                                    : lower(got) == "true";
    // An unknown relation holds for nothing, so the workflow stops rather
    // than carrying on as though a condition had been met.
    return false;
}

} // namespace dbal::workflow
