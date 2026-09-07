#pragma once
#include "workflow/wf_step.hpp"
#include "workflow/wf_stop.hpp"
#include "workflow/wf_compare.hpp"
#include <stdexcept>

namespace dbal::workflow::steps {

/**
 * dbal.stop.unless — Go no further unless a condition holds.
 *
 * parameters: { "value": "${event.data.job}",
 *               "is": "not empty" | "equals" | "contains" | "empty",
 *               "other": "wheel" }
 *
 * This is how a linear executor gets a decision. Real branching would
 * need the executor to follow edges rather than run a list, which is a
 * much larger change; stopping early covers most of what a workflow
 * actually wants -- "only do this when the form said X" -- without
 * pretending to be a graph engine.
 *
 * Stopping is not a failure: it throws WfStop, which the executor treats
 * as the workflow finishing early rather than as an error to log.
 */
class StopUnlessStep : public IWfStep {
public:
    std::string type() const override { return "dbal.stop.unless"; }
    void execute(const WfNode& node, WfContext&, dbal::Client&) override {
        auto& p = node.parameters;
        const std::string is = p.value("is", std::string("not empty"));
        const nlohmann::json value = p.value("value", nlohmann::json{});
        const nlohmann::json other = p.value("other", nlohmann::json{});

        if (!compareHolds(is, value, other))
            throw WfStop("condition '" + is + "' did not hold");
    }
};

} // namespace dbal::workflow::steps
