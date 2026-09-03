/**
 * @file bql_route_handler.hpp
 * @brief Stateless BQL syntax parsing -- turns a "controlled English"
 *        script into a generic sentence AST (add/give/style/apply), with
 *        no knowledge of any app's block names or properties. That
 *        semantic layer is each calling app's own job (see
 *        frontends/nextjs/.../bql/apply.ts for the Next.js resolver
 *        against its PALETTE/PROP_SCHEMAS); this endpoint exists so the
 *        syntax layer is written once and shared, not reimplemented by
 *        every app that wants a plain-English authoring surface.
 *
 * Needs no dbal::Client -- parsing touches no database and no tenant
 * data, so this handler carries no per-request DB dependency at all.
 */
#pragma once

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <functional>
#include <string>

namespace dbal::daemon::handlers::bql {

class BqlRouteHandler {
public:
    using DrogonCallback = std::function<void(const drogon::HttpResponsePtr&)>;

    /// POST /{tenant}/{package}/bql/parse — body {"script": "..."}.
    /// Returns {"ok": true, "sentences": [...]} or
    /// {"ok": false, "errors": [{"line": N, "message": "..."}]} -- never a
    /// partial sentence list, matching parseScript's fail-closed contract.
    void handle(const drogon::HttpRequestPtr& req, DrogonCallback&& cb,
                const std::string& tenant, const std::string& package);
};

} // namespace dbal::daemon::handlers::bql
