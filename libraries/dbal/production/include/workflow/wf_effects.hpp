#pragma once
#include <nlohmann/json.hpp>
#include <string>

namespace dbal::workflow {

/**
 * The context variable a workflow's page effects accumulate under.
 *
 * A step that changes the page cannot change it from here: the DOM is in
 * somebody's browser and this is a daemon. So a `page.*` step records what
 * it wants done, and the list travels back in the response for the browser
 * to apply. The workflow still decides -- it just cannot reach the page
 * itself, which is also what stops a workflow reaching a page it was never
 * invoked from.
 */
inline constexpr const char* kEffectsVar = "__page_effects";

/** Append one effect to the list in @p ctx_value, returning the new list. */
nlohmann::json appendEffect(const nlohmann::json& existing,
                            const nlohmann::json& effect);

} // namespace dbal::workflow
