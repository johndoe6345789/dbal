/**
 * @file process_version.hpp
 * @brief Handle version endpoint
 */

#pragma once

#include "build_info.hpp"
#include <string>
#include "../request/http_request.hpp"
#include "../response/http_response.hpp"

namespace dbal {
namespace daemon {

/**
 * @brief Check if request is a version request and process it
 * @param request HTTP request
 * @param response HTTP response (populated if version request)
 * @return true if this was a version request
 */
inline bool process_version(
    const HttpRequest& request,
    HttpResponse& response
) {
    if (request.path == "/api/version" || request.path == "/version") {
        response.body = std::string(R"({"version":")") + dbal::kVersion +
                        R"(","service":")" + dbal::kServiceName +
                        R"(","commit":")" + dbal::gitCommit() + R"("})";
        return true;
    }
    return false;
}

} // namespace daemon
} // namespace dbal
