#include "bql_route_handler.hpp"
#include "../../json_convert.hpp"
#include "bql/bql_parser.hpp"
#include <json/json.h>

namespace dbal::daemon::handlers::bql {

namespace {

constexpr size_t kMaxScriptLength = 50000;

void sendError(const BqlRouteHandler::DrogonCallback& cb,
               drogon::HttpStatusCode code, const std::string& message) {
    ::Json::Value body;
    body["ok"] = false;
    body["error"] = message;
    auto resp = drogon::HttpResponse::newHttpJsonResponse(body);
    resp->setStatusCode(code);
    cb(resp);
}

} // namespace

void BqlRouteHandler::handle(const drogon::HttpRequestPtr& req, DrogonCallback&& cb,
                              const std::string& /* tenant */,
                              const std::string& /* package */) {
    auto jsonPtr = req->getJsonObject();
    if (!jsonPtr || !jsonPtr->isMember("script") || !(*jsonPtr)["script"].isString()) {
        sendError(cb, drogon::k400BadRequest, "Body must be {\"script\": \"...\"}");
        return;
    }

    const std::string script = (*jsonPtr)["script"].asString();
    if (script.size() > kMaxScriptLength) {
        sendError(cb, drogon::k413RequestEntityTooLarge,
                   "Script exceeds the " + std::to_string(kMaxScriptLength) +
                       "-character limit");
        return;
    }

    dbal::bql::ScriptParseResult result = dbal::bql::parseScript(script);
    nlohmann::json body = dbal::bql::toJson(result);
    auto resp = drogon::HttpResponse::newHttpJsonResponse(
        dbal::daemon::nlohmann_to_jsoncpp(body));
    resp->setStatusCode(result.ok ? drogon::k200OK : drogon::k422UnprocessableEntity);
    cb(resp);
}

} // namespace dbal::daemon::handlers::bql
