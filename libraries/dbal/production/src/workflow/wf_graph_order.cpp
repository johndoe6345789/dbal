#include "workflow/wf_graph_order.hpp"
#include <deque>
#include <map>
#include <set>

namespace dbal::workflow {

std::vector<std::string> topologicalOrder(
    const std::vector<std::string>& node_keys,
    const std::vector<std::pair<std::string, std::string>>& edges) {
    const std::set<std::string> known(node_keys.begin(), node_keys.end());
    std::map<std::string, std::vector<std::string>> out;
    std::map<std::string, int> indegree;
    for (const auto& key : node_keys) indegree[key] = 0;

    for (const auto& [from, to] : edges) {
        // An edge naming a node that is not here cannot order anything.
        if (known.count(from) == 0 || known.count(to) == 0) continue;
        out[from].push_back(to);
        indegree[to] += 1;
    }

    // Seeded in the caller's order, so nodes no edge constrains keep the
    // order they were given rather than an arbitrary one.
    std::deque<std::string> ready;
    for (const auto& key : node_keys)
        if (indegree[key] == 0) ready.push_back(key);

    std::vector<std::string> ordered;
    while (!ready.empty()) {
        const std::string key = ready.front();
        ready.pop_front();
        ordered.push_back(key);
        for (const auto& next : out[key])
            if (--indegree[next] == 0) ready.push_back(next);
    }

    if (ordered.size() != node_keys.size()) return node_keys;
    return ordered;
}

nlohmann::json decodeParam(const std::string& value, const std::string& type) {
    if (type == "number") {
        try {
            size_t used = 0;
            const double n = std::stod(value, &used);
            // "12abc" is not a number; a partial parse must not become one.
            if (used == value.size()) return nlohmann::json(n);
        } catch (...) {
            // Falls through to the string below.
        }
        return nlohmann::json(value);
    }
    if (type == "boolean") return nlohmann::json(value == "true");
    if (type == "json") {
        try { return nlohmann::json::parse(value); }
        catch (...) { return nlohmann::json(value); }
    }
    return nlohmann::json(value);
}

} // namespace dbal::workflow
