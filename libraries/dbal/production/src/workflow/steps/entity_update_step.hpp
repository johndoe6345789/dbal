#pragma once
#include "workflow/wf_step.hpp"
#include <stdexcept>

namespace dbal::workflow::steps {

/**
 * dbal.entity.update — Change an existing row.
 *
 * parameters: { "entity": "FormSubmission",
 *               "id": "${event.id}",
 *               "data": { "status": "handled" } }
 * outputs:    { "result": "ctx_variable_name" }
 *
 * The counterpart to dbal.entity.create, which was the only way to write
 * anything: a workflow could record a booking but never mark it dealt
 * with, so anything that needed a second stage could not be built at all.
 */
class EntityUpdateStep : public IWfStep {
public:
    std::string type() const override { return "dbal.entity.update"; }
    void execute(const WfNode& node, WfContext& ctx, dbal::Client& client) override {
        auto& p = node.parameters;
        if (!p.contains("entity") || !p["entity"].is_string())
            throw std::runtime_error("dbal.entity.update: missing 'entity' parameter");
        if (!p.contains("id") || !p["id"].is_string())
            throw std::runtime_error("dbal.entity.update: missing 'id' parameter");
        const std::string entity = p["entity"].get<std::string>();
        const std::string id     = p["id"].get<std::string>();
        const nlohmann::json data = p.value("data", nlohmann::json::object());

        auto result = client.updateEntity(entity, id, data);
        if (!result.isOk())
            throw std::runtime_error("dbal.entity.update [" + entity + "]: " +
                                     std::string(result.error().what()));

        if (node.outputs.is_object()) {
            for (auto& [k, v] : node.outputs.items())
                if (v.is_string()) ctx.set(v.get<std::string>(), result.value());
        }
    }
};

} // namespace dbal::workflow::steps
