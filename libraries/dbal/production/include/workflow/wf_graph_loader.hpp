#pragma once
#include "workflow/wf_step.hpp"
#include "workflow/wf_graph_order.hpp"
#include "dbal/core/client.hpp"
#include <optional>
#include <string>
#include <vector>

namespace dbal::workflow {

/** A tenant's workflow, rebuilt from the rows the God Panel published. */
struct LoadedWorkflow {
    std::string id;
    std::string name;
    std::vector<WfNode> nodes;
};

/**
 * The workflow a tenant published for @p trigger_event, rebuilt from rows.
 *
 * Workflows used to come only from JSON files baked into this image, listed
 * in a static event_config.json -- so a workflow built in the God Panel was
 * stored faithfully and then read by nothing. This is the other half: the
 * same graph, reassembled from WorkflowNode / WorkflowNodeParam /
 * WorkflowEdge rows into exactly the WfNode list the executor already runs.
 *
 * @p trigger_event is the entity half of the event name, "Booking.created",
 * not the tenant-qualified form -- the tenant is a column, not part of what
 * a tenant writes.
 *
 * Nodes come back in the order the edges imply, not the order they were
 * stored: the editor is a canvas, so stored order is where boxes were
 * dropped, which has nothing to do with what must happen first.
 *
 * Returns nullopt when the tenant has published no workflow for the event.
 * Never throws: a workflow that cannot be read must not take down the write
 * that triggered it.
 */
/**
 * @param form Which form the submission came from. A workflow naming that
 *             form wins over one naming none; one naming a different form
 *             does not answer at all. Without this every workflow
 *             subscribed to FormSubmission.created answered every form on
 *             the tenant, and which of three ran came down to whichever
 *             the database happened to return first.
 */
std::optional<LoadedWorkflow> loadTenantWorkflow(dbal::Client& client,
                                                 const std::string& tenant,
                                                 const std::string& trigger_event,
                                                 const std::string& form);

/**
 * The workflow @p named, if it has opted in to @p trigger_event.
 *
 * A button says which workflow it runs, rather than leaving the connection
 * to live in some workflow's own settings. But the name arrives in a
 * request body, and FormSubmission may be created by anyone with no
 * account -- so on its own that would let a stranger run any workflow a
 * tenant had ever published, and workflows create and read rows.
 *
 * The workflow's own trigger is the opt-in. A tenant marks a workflow
 * "runs when someone submits a form" and it becomes reachable from a
 * page; naming only chooses among the ones already willing. A workflow
 * with any other trigger, or none, cannot be summoned by a submission
 * however it is named.
 *
 * @p named is matched against the id first and then the name: a name is
 * what someone types and can collide, an id cannot. Drafts are skipped.
 * Returns nullopt when nothing matches, which is logged -- a button
 * pointing at a workflow that will not run must not fail silently.
 */
std::optional<LoadedWorkflow> loadTenantWorkflowNamed(dbal::Client& client,
                                                      const std::string& tenant,
                                                      const std::string& named,
                                                      const std::string& trigger_event,
                                                      const std::string& form);

} // namespace dbal::workflow
