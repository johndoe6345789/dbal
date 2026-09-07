#include "workflow/wf_effects.hpp"

namespace dbal::workflow {

nlohmann::json appendEffect(const nlohmann::json& existing,
                            const nlohmann::json& effect) {
    nlohmann::json list = existing.is_array() ? existing
                                              : nlohmann::json::array();
    list.push_back(effect);
    return list;
}

} // namespace dbal::workflow
