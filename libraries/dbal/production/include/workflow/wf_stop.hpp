#pragma once
#include <stdexcept>
#include <string>

namespace dbal::workflow {

/**
 * A workflow deciding it is done, rather than failing.
 *
 * Thrown by dbal.stop.unless when its condition does not hold. The
 * executor catches it and stops quietly: a workflow that correctly
 * decided not to act is not an error, and logging it as one would train
 * everyone to ignore the errors that matter.
 */
class WfStop : public std::runtime_error {
public:
    explicit WfStop(const std::string& why)
        : std::runtime_error("workflow stopped: " + why) {}
};

} // namespace dbal::workflow
