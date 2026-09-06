#pragma once
#include <nlohmann/json.hpp>
#include <string>
#include <utility>
#include <vector>

namespace dbal::workflow {

/**
 * Order @p node_keys so every edge runs source-before-target.
 *
 * The editor is a canvas, so the order nodes were stored in is the order
 * boxes were dropped on it -- which has nothing to do with what has to
 * happen first. The edges are the only thing that says that.
 *
 * A cycle cannot be ordered, so the input order is returned unchanged
 * rather than dropping the nodes caught in it: a workflow that runs in a
 * surprising order is easier to diagnose than one that silently does half
 * its work. Edges naming a node that is not present are ignored.
 */
std::vector<std::string> topologicalOrder(
    const std::vector<std::string>& node_keys,
    const std::vector<std::pair<std::string, std::string>>& edges);

/**
 * A stored parameter, back as the value it was before storage.
 *
 * The type column decides, not the shape of the text -- a string that
 * merely looks like JSON stays a string, exactly as on the writing side
 * (see workflow-graph/param-value.ts). Anything unparseable falls back to
 * its own text, which is at least visible and fixable in the editor.
 */
nlohmann::json decodeParam(const std::string& value, const std::string& type);

} // namespace dbal::workflow
