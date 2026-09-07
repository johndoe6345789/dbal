#pragma once
#include "workflow/wf_step.hpp"
#include "dbal/core/types.hpp"
#include <stdexcept>

namespace dbal::workflow::steps {

/**
 * dbal.entity.count — How many rows match a filter.
 *
 * parameters: { "entity": "FormSubmission",
 *               "filter": { "formName": "book-a-repair" } }
 * outputs:    { "count": "ctx_variable_name" }
 *
 * Counting through dbal.entity.list meant fetching every row to measure
 * them, which is fine for ten and not for ten thousand.
 */
class EntityCountStep : public IWfStep {
public:
    std::string type() const override { return "dbal.entity.count"; }
    void execute(const WfNode& node, WfContext& ctx, dbal::Client& client) override {
        auto& p = node.parameters;
        if (!p.contains("entity") || !p["entity"].is_string())
            throw std::runtime_error("dbal.entity.count: missing 'entity' parameter");
        const std::string entity = p["entity"].get<std::string>();

        ListOptions opts;
        if (p.contains("filter") && p["filter"].is_object()) {
            for (auto& [k, v] : p["filter"].items())
                if (v.is_string()) opts.filter[k] = v.get<std::string>();
        }

        auto result = client.listEntities(entity, opts);
        if (!result.isOk())
            throw std::runtime_error("dbal.entity.count [" + entity + "]: " +
                                     std::string(result.error().what()));

        const auto count = static_cast<long long>(result.value().items.size());
        if (node.outputs.is_object()) {
            for (auto& [k, v] : node.outputs.items())
                if (v.is_string()) ctx.set(v.get<std::string>(), count);
        }
    }
};

} // namespace dbal::workflow::steps
