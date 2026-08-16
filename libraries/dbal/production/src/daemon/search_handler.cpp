#include "search_handler.hpp"
#include "json_convert.hpp"
#include <spdlog/spdlog.h>

namespace dbal {
namespace daemon {
namespace rpc {

void SearchHandler::handleSearch(
    Client& client,
    const RouteInfo& route,
    const std::string& query,
    int limit,
    ResponseSender send_success,
    ErrorSender send_error
) {
    spdlog::trace("SearchHandler::handleSearch: tenant='{}', entity='{}', limit={}",
                  route.tenant, route.entity, limit);

    ResponseFormatter::withExceptionHandling([&]() {
        auto result = client.searchEntities(route.entity, query, limit);
        if (!result.isOk()) {
            // 503 rather than 500: the usual cause is that search is not
            // configured, or Elasticsearch is unreachable. Both are "this
            // capability is unavailable right now", not "your request was
            // wrong" and not a bug in the daemon.
            ResponseFormatter::sendError(result.error().what(), 503, send_error);
            return;
        }

        const auto& hits = result.value();
        ::Json::Value items(::Json::arrayValue);
        for (const auto& item : hits.items) {
            items.append(nlohmann_to_jsoncpp(item));
        }

        ::Json::Value data;
        data["data"] = items;
        // "total" is Elasticsearch's match count, which can exceed the number
        // of items returned -- limit caps the page, not the result set.
        data["total"] = hits.total;
        data["limit"] = hits.limit;
        // Named so a caller cannot mistake these for a consistent read of the
        // primary: the mirror is best-effort and can lag or drift.
        data["source"] = "search-index";

        ResponseFormatter::sendSuccess(data, send_success);
    }, send_error);
}

} // namespace rpc
} // namespace daemon
} // namespace dbal
