#pragma once
#include "workflow/wf_step.hpp"
#include <stdexcept>

namespace dbal::workflow::steps {

/**
 * dbal.entity.delete — Remove a row.
 *
 * parameters: { "entity": "FormSubmission", "id": "${event.id}" }
 * outputs:    { "removed": "ctx_variable_name" }  (a boolean)
 */
class EntityDeleteStep : public IWfStep {
public:
    std::string type() const override { return "dbal.entity.delete"; }
    void execute(const WfNode& node, WfContext& ctx, dbal::Client& client) override {
        auto& p = node.parameters;
        if (!p.contains("entity") || !p["entity"].is_string())
            throw std::runtime_error("dbal.entity.delete: missing 'entity' parameter");
        if (!p.contains("id") || !p["id"].is_string())
            throw std::runtime_error("dbal.entity.delete: missing 'id' parameter");
        const std::string entity = p["entity"].get<std::string>();

        auto result = client.deleteEntity(entity, p["id"].get<std::string>());
        if (!result.isOk())
            throw std::runtime_error("dbal.entity.delete [" + entity + "]: " +
                                     std::string(result.error().what()));

        if (node.outputs.is_object()) {
            for (auto& [k, v] : node.outputs.items())
                if (v.is_string()) ctx.set(v.get<std::string>(), result.value());
        }
    }
};

} // namespace dbal::workflow::steps
