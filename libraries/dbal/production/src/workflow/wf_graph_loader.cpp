#include "workflow/wf_graph_loader.hpp"
#include "workflow/wf_graph_order.hpp"
#include "dbal/core/types.hpp"
#include <spdlog/spdlog.h>
#include <map>
#include <vector>

namespace dbal::workflow {

namespace {

/** Every row of @p entity belonging to this tenant and workflow. */
std::vector<nlohmann::json> rowsFor(dbal::Client& client,
                                    const std::string& entity,
                                    const std::string& tenant,
                                    const std::string& workflow_id) {
    ListOptions opts;
    opts.filter["tenantId"]   = tenant;
    opts.filter["workflowId"] = workflow_id;
    opts.limit = 2000;
    auto result = client.listEntities(entity, opts);
    if (!result.isOk()) {
        spdlog::warn("[workflow] could not read {} for {}: {}", entity,
                     workflow_id, std::string(result.error().what()));
        return {};
    }
    std::vector<nlohmann::json> rows;
    for (const auto& item : result.value().items) rows.push_back(item);
    return rows;
}

std::string str(const nlohmann::json& row, const char* key) {
    if (!row.contains(key) || row[key].is_null()) return {};
    if (row[key].is_string()) return row[key].get<std::string>();
    return row[key].dump();
}

} // namespace

std::optional<LoadedWorkflow> loadTenantWorkflow(dbal::Client& client,
                                                 const std::string& tenant,
                                                 const std::string& trigger_event) {
    try {
        ListOptions opts;
        opts.filter["tenantId"]     = tenant;
        opts.filter["triggerEvent"] = trigger_event;
        opts.limit = 10;
        auto found = client.listEntities("Workflow", opts);
        if (!found.isOk()) {
            spdlog::warn("[workflow] could not look up workflows for {}.{}: {}",
                         tenant, trigger_event,
                         std::string(found.error().what()));
            return std::nullopt;
        }

        nlohmann::json chosen;
        for (const auto& row : found.value().items) {
            // A draft is a work in progress, not something a visitor's
            // submission should set running.
            if (row.contains("isPublished") && row["isPublished"].is_boolean()
                && !row["isPublished"].get<bool>()) continue;
            chosen = row;
            break;
        }
        if (chosen.is_null()) return std::nullopt;

        LoadedWorkflow wf;
        wf.id   = str(chosen, "id");
        wf.name = str(chosen, "name");
        if (wf.id.empty()) return std::nullopt;

        auto node_rows  = rowsFor(client, "WorkflowNode", tenant, wf.id);
        if (node_rows.empty()) {
            spdlog::warn("[workflow] {} '{}' has no nodes", tenant, wf.name);
            return std::nullopt;
        }
        auto param_rows = rowsFor(client, "WorkflowNodeParam", tenant, wf.id);
        auto edge_rows  = rowsFor(client, "WorkflowEdge", tenant, wf.id);

        // Parameters arrive as one row each; regroup them per node. `outputs`
        // is a parameter by name because the editor has no separate field
        // for it, and the executor needs it to wire a step's result into a
        // context variable the next step can read.
        std::map<std::string, nlohmann::json> params_by_node;
        std::map<std::string, nlohmann::json> outputs_by_node;
        for (const auto& row : param_rows) {
            const std::string node_id = str(row, "nodeId");
            const std::string name    = str(row, "name");
            if (node_id.empty() || name.empty()) continue;
            auto value = decodeParam(str(row, "value"), str(row, "valueType"));
            if (name == "outputs") {
                if (value.is_object()) outputs_by_node[node_id] = std::move(value);
                continue;
            }
            if (!params_by_node.count(node_id))
                params_by_node[node_id] = nlohmann::json::object();
            params_by_node[node_id][name] = std::move(value);
        }

        std::vector<std::string> keys;
        std::map<std::string, nlohmann::json> row_by_key;
        for (const auto& row : node_rows) {
            const std::string key = str(row, "nodeKey");
            if (key.empty()) continue;
            keys.push_back(key);
            row_by_key[key] = row;
        }

        std::vector<std::pair<std::string, std::string>> edges;
        for (const auto& row : edge_rows) {
            const std::string from = str(row, "sourceKey");
            const std::string to   = str(row, "targetKey");
            if (!from.empty() && !to.empty()) edges.emplace_back(from, to);
        }

        for (const auto& key : topologicalOrder(keys, edges)) {
            const auto& row = row_by_key[key];
            WfNode node;
            node.id   = key;
            node.type = str(row, "type");
            const std::string node_id = str(row, "id");
            node.parameters = params_by_node.count(node_id)
                ? params_by_node[node_id] : nlohmann::json::object();
            node.outputs = outputs_by_node.count(node_id)
                ? outputs_by_node[node_id] : nlohmann::json::object();
            wf.nodes.push_back(std::move(node));
        }
        return wf;
    } catch (const std::exception& e) {
        spdlog::error("[workflow] loading {}.{} failed: {}", tenant,
                      trigger_event, e.what());
        return std::nullopt;
    } catch (...) {
        spdlog::error("[workflow] loading {}.{} failed", tenant, trigger_event);
        return std::nullopt;
    }
}

} // namespace dbal::workflow
