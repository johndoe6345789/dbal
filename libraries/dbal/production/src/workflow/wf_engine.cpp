#include "wf_engine.hpp"
#include "dbal/core/types.hpp"
#include "steps/uuid_step.hpp"
#include "steps/timestamp_step.hpp"
#include "steps/entity_create_step.hpp"
#include "steps/entity_get_step.hpp"
#include "steps/entity_list_step.hpp"
#include "steps/var_set_step.hpp"
#include "steps/log_step.hpp"
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <thread>

namespace dbal::workflow {

WfEngine::WfEngine(const dbal::ClientConfig& client_config)
    : client_config_(client_config) {
    // Register all built-in DBAL workflow steps
    executor_.registerStep(std::make_shared<steps::UuidStep>());
    executor_.registerStep(std::make_shared<steps::TimestampStep>());
    executor_.registerStep(std::make_shared<steps::EntityCreateStep>());
    executor_.registerStep(std::make_shared<steps::EntityGetStep>());
    executor_.registerStep(std::make_shared<steps::EntityListStep>());
    executor_.registerStep(std::make_shared<steps::VarSetStep>());
    executor_.registerStep(std::make_shared<steps::LogStep>());
}

void WfEngine::loadConfig(const std::string& json_path) {
    try {
        std::ifstream f(json_path);
        if (!f.is_open()) throw std::runtime_error("Cannot open file");
        nlohmann::json root = nlohmann::json::parse(f);

        if (!root.contains("events") || !root["events"].is_array()) {
            spdlog::warn("[workflow] event_config has no 'events' list: {}", json_path);
            return;
        }
        for (const auto& entry : root["events"]) {
            std::string event    = entry.value("event",    std::string(""));
            std::string workflow = entry.value("workflow", std::string(""));
            if (!event.empty() && !workflow.empty()) {
                event_map_[event] = workflow;
                spdlog::debug("[workflow] registered: {} → {}", event, workflow);
            }
        }
        spdlog::info("[workflow] loaded {} event mappings from {}", event_map_.size(), json_path);
    } catch (const std::exception& e) {
        spdlog::error("[workflow] failed to load event config '{}': {}", json_path, e.what());
    }
}

namespace {

/** How long a cached set of tenant triggers is trusted before re-reading. */
constexpr std::chrono::seconds kTenantEventsTtl{30};

/** "acme.Booking.created" -> ("acme", "Booking.created"); empty on a name
 *  that is not tenant-qualified. */
std::pair<std::string, std::string> splitEvent(const std::string& event_name) {
    const auto dot = event_name.find('.');
    if (dot == std::string::npos || dot == 0 || dot + 1 >= event_name.size())
        return {};
    return {event_name.substr(0, dot), event_name.substr(dot + 1)};
}

} // namespace

void WfEngine::invalidateTenantEvents() const {
    std::lock_guard<std::mutex> guard(tenant_events_lock_);
    tenant_events_loaded_ = false;
}

void WfEngine::refreshTenantEvents() const {
    const auto now = std::chrono::steady_clock::now();
    {
        std::lock_guard<std::mutex> guard(tenant_events_lock_);
        if (tenant_events_loaded_ && now - tenant_events_at_ < kTenantEventsTtl)
            return;
    }

    std::unordered_set<std::string> found;
    try {
        dbal::Client client(client_config_);
        ListOptions opts;
        opts.limit = 2000;
        auto result = client.listEntities("Workflow", opts);
        if (result.isOk()) {
            for (const auto& row : result.value().items) {
                if (!row.contains("triggerEvent") || !row["triggerEvent"].is_string())
                    continue;
                const auto trigger = row["triggerEvent"].get<std::string>();
                if (trigger.empty()) continue;
                if (row.contains("isPublished") && row["isPublished"].is_boolean()
                    && !row["isPublished"].get<bool>()) continue;
                std::string tenant = "system";
                if (row.contains("tenantId") && row["tenantId"].is_string()
                    && !row["tenantId"].get<std::string>().empty())
                    tenant = row["tenantId"].get<std::string>();
                found.insert(tenant + "." + trigger);
            }
        } else {
            spdlog::warn("[workflow] could not list tenant workflows: {}",
                         std::string(result.error().what()));
        }
    } catch (const std::exception& e) {
        // A cache that cannot be filled must not stop writes: the worst
        // case is a tenant workflow that does not fire, not a failed POST.
        spdlog::warn("[workflow] tenant trigger refresh failed: {}", e.what());
    } catch (...) {
        spdlog::warn("[workflow] tenant trigger refresh failed");
    }

    std::lock_guard<std::mutex> guard(tenant_events_lock_);
    tenant_events_      = std::move(found);
    tenant_events_at_   = std::chrono::steady_clock::now();
    tenant_events_loaded_ = true;
}

bool WfEngine::hasEvent(const std::string& event_name) const {
    if (event_map_.count(event_name) > 0) return true;
    refreshTenantEvents();
    std::lock_guard<std::mutex> guard(tenant_events_lock_);
    return tenant_events_.count(event_name) > 0;
}

void WfEngine::dispatchAsync(const std::string& event_name,
                              const nlohmann::json& entity_data) const {
    auto it = event_map_.find(event_name);
    const bool from_file = it != event_map_.end();
    std::string workflow_path = from_file ? it->second : std::string();
    if (!from_file && !hasEvent(event_name)) return;

    nlohmann::json data_copy  = entity_data; // copy for thread safety

    spdlog::debug("[workflow] dispatching async: {}", event_name);

    // Capture executor_ and client_config_ by value so the detached thread
    // owns everything it needs independently of `this`.
    WfExecutor exec_copy = executor_;
    dbal::ClientConfig cfg_copy = client_config_;

    std::thread([exec_copy = std::move(exec_copy), cfg_copy = std::move(cfg_copy),
                 workflow_path, from_file, data_copy, event_name]() mutable {
        try {
            WfContext ctx;
            ctx.set("event", data_copy); // accessible as ${event.userId}, ${event.tenantId}, etc.

            dbal::Client client(cfg_copy);
            if (from_file) {
                exec_copy.execute(workflow_path, ctx, client);
            } else {
                // Published from the God Panel rather than shipped in this
                // image: same executor, nodes rebuilt from rows.
                const auto [tenant, trigger] = splitEvent(event_name);
                auto loaded = loadTenantWorkflow(client, tenant, trigger);
                if (!loaded) {
                    spdlog::info("[workflow] {} has no published workflow", event_name);
                    return;
                }
                exec_copy.executeNodes(loaded->nodes, loaded->name, ctx, client);
            }
            spdlog::info("[workflow] {} completed", event_name);
        } catch (const std::exception& e) {
            spdlog::error("[workflow] {} failed: {}", event_name, e.what());
        } catch (...) {
            spdlog::error("[workflow] {} failed: unknown error", event_name);
        }
    }).detach();
}

} // namespace dbal::workflow
