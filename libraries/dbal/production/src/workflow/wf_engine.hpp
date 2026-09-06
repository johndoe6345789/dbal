#pragma once
#include "wf_executor.hpp"
#include "workflow/wf_graph_loader.hpp"
#include "dbal/core/client.hpp"
#include <nlohmann/json.hpp>
#include <chrono>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace dbal::workflow {

/**
 * WorkflowEngine — maps DBAL CRUD events to workflow files and dispatches them.
 *
 * Loaded from YAML event config (DBAL_EVENT_CONFIG env var):
 *   events:
 *     - event: "pastebin.User.created"
 *       workflow: "/app/schemas/workflows/pastebin/on_user_created.json"
 *
 * Dispatch is non-blocking: each workflow runs in a detached std::thread
 * with its own dbal::Client instance. Errors are logged, never propagated.
 */
class WfEngine {
public:
    explicit WfEngine(const dbal::ClientConfig& client_config);

    // Load event→workflow mappings from YAML file
    void loadConfig(const std::string& yaml_path);

    // Fire-and-forget: if event_name is mapped, run its workflow async.
    // entity_data is the created/updated entity JSON from the CRUD handler.
    void dispatchAsync(const std::string& event_name,
                       const nlohmann::json& entity_data) const;

    /**
     * Whether anything would run for @p event_name.
     *
     * Two sources now: the static file map above, and whatever tenants have
     * published from the God Panel. The second is cached, because this is
     * asked on every single create and the answer is almost always no --
     * without a cache that would be a database round trip per write.
     */
    bool hasEvent(const std::string& event_name) const;

    /** Forget the cached tenant triggers, e.g. after a Workflow is written. */
    void invalidateTenantEvents() const;

private:
    /** Refill tenant_events_ if it has gone stale. */
    void refreshTenantEvents() const;

    dbal::ClientConfig client_config_;
    WfExecutor executor_;
    std::unordered_map<std::string, std::string> event_map_; // event → workflow path

    // "<tenant>.<Entity>.created" for every published tenant workflow.
    mutable std::mutex tenant_events_lock_;
    mutable std::unordered_set<std::string> tenant_events_;
    mutable std::chrono::steady_clock::time_point tenant_events_at_{};
    mutable bool tenant_events_loaded_ = false;
};

} // namespace dbal::workflow
