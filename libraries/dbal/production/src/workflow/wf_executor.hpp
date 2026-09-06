#pragma once
#include "workflow/wf_context.hpp"
#include "workflow/wf_step.hpp"
#include "dbal/core/client.hpp"
#include <nlohmann/json.hpp>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace dbal::workflow {

class WfExecutor {
public:
    void registerStep(std::shared_ptr<IWfStep> step);

    // Load workflow JSON from file and execute all nodes in array order.
    // Parameters in each node are resolved against ctx before the step runs.
    // Unknown step types are skipped with a warning (same as gameengine executor).
    void execute(const std::string& workflow_path, WfContext& ctx, dbal::Client& client) const;

    /**
     * Execute nodes already in hand, in the order given.
     *
     * The file above is no longer the only source: a tenant builds a
     * workflow in the God Panel and it is stored as WorkflowNode rows, not
     * as a file in this image. @p label names the workflow in the log, so a
     * DB-loaded run is as traceable as a file-loaded one.
     */
    void executeNodes(const std::vector<WfNode>& nodes, const std::string& label,
                      WfContext& ctx, dbal::Client& client) const;

    /** Parse a workflow document's "nodes" array. @p label is for errors. */
    static std::vector<WfNode> parseNodes(const nlohmann::json& doc,
                                          const std::string& label);

private:
    std::unordered_map<std::string, std::shared_ptr<IWfStep>> steps_;

    static std::vector<WfNode> loadNodes(const std::string& path);
};

} // namespace dbal::workflow
