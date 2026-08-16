#ifndef DBAL_SEARCH_HANDLER_HPP
#define DBAL_SEARCH_HANDLER_HPP

#include "response_formatter.hpp"
#include "rpc_restful_handler.hpp"
#include "dbal/core/client.hpp"
#include <string>

namespace dbal {
namespace daemon {
namespace rpc {

/**
 * @brief Handler for full-text search.
 *
 * GET /{tenant}/{package}/{entity}/_search?q=...&limit=...
 *
 * Answered by the Elasticsearch mirror that SearchingAdapter maintains when
 * DBAL_SEARCH_URL is set. With it unset the adapter chain has no search layer
 * and the request fails with an explanatory error rather than an empty result
 * set -- "search is switched off" and "nothing matched your query" are
 * different answers and must not look alike.
 */
class SearchHandler {
public:
    /**
     * @brief Handle a search request
     * @param client DBAL client
     * @param route Parsed route information
     * @param query Free-text query
     * @param limit Maximum hits
     * @param send_success Success callback
     * @param send_error Error callback
     */
    static void handleSearch(
        Client& client,
        const RouteInfo& route,
        const std::string& query,
        int limit,
        ResponseSender send_success,
        ErrorSender send_error
    );
};

} // namespace rpc
} // namespace daemon
} // namespace dbal

#endif // DBAL_SEARCH_HANDLER_HPP
