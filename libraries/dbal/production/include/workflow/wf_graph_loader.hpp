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
std::optional<LoadedWorkflow> loadTenantWorkflow(dbal::Client& client,
                                                 const std::string& tenant,
                                                 const std::string& trigger_event);

} // namespace dbal::workflow
