#pragma once
#include "workflow/wf_step.hpp"
#include "workflow/wf_effects.hpp"
#include <stdexcept>

namespace dbal::workflow::steps {

/**
 * The `page.*` steps: things a workflow wants done to the page it was
 * invoked from.
 *
 * None of them touch a DOM -- this is a daemon, and the page is in
 * somebody's browser. Each records what it wants as JSON, and the list
 * travels back in the response for the browser to apply. That keeps the
 * deciding here, where the workflow lives, and the doing there, where the
 * page is.
 *
 * `target` is a CSS selector, matched against the page the click came
 * from. A step naming a target that is not on the page does nothing, in
 * the browser rather than here.
 */
class PageEffectStep : public IWfStep {
public:
    PageEffectStep(std::string type, std::vector<std::string> required)
        : type_(std::move(type)), required_(std::move(required)) {}

    std::string type() const override { return type_; }

    void execute(const WfNode& node, WfContext& ctx, dbal::Client&) override {
        for (const auto& name : required_) {
            if (!node.parameters.contains(name))
                throw std::runtime_error(type_ + ": missing '" + name +
                                         "' parameter");
        }
        nlohmann::json effect = node.parameters;
        effect["do"] = type_;
        ctx.set(kEffectsVar, appendEffect(ctx.get(kEffectsVar), effect));
    }

private:
    std::string type_;
    std::vector<std::string> required_;
};

/** page.text — replace the text of everything matching `target`. */
inline std::shared_ptr<IWfStep> pageTextStep() {
    return std::make_shared<PageEffectStep>("page.text",
                                            std::vector<std::string>{"target", "text"});
}

/** page.show / page.hide — reveal or conceal everything matching. */
inline std::shared_ptr<IWfStep> pageShowStep() {
    return std::make_shared<PageEffectStep>("page.show",
                                            std::vector<std::string>{"target"});
}
inline std::shared_ptr<IWfStep> pageHideStep() {
    return std::make_shared<PageEffectStep>("page.hide",
                                            std::vector<std::string>{"target"});
}

/** page.class — add or remove a class, so a tenant's own styles can act. */
inline std::shared_ptr<IWfStep> pageClassStep() {
    return std::make_shared<PageEffectStep>(
        "page.class", std::vector<std::string>{"target", "class"});
}

/** page.message — say something to whoever clicked. */
inline std::shared_ptr<IWfStep> pageMessageStep() {
    return std::make_shared<PageEffectStep>("page.message",
                                            std::vector<std::string>{"text"});
}

/** page.go — send the browser to another page of this site. */
inline std::shared_ptr<IWfStep> pageGoStep() {
    return std::make_shared<PageEffectStep>("page.go",
                                            std::vector<std::string>{"path"});
}

} // namespace dbal::workflow::steps
