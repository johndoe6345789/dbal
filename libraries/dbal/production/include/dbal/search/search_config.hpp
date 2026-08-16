/**
 * @file search_config.hpp
 * @brief Parses DBAL_SEARCH_URL into the pieces the search layer needs.
 *
 * Format, as documented in .env.example:
 *
 *   http://localhost:9200?index=dbal_search&refresh=true
 *
 * This is the *secondary* search layer, not the elasticsearch adapter. Setting
 * DATABASE_URL to an elasticsearch URL makes Elasticsearch the authoritative
 * store; setting DBAL_SEARCH_URL mirrors writes from whatever the primary is
 * into Elasticsearch so full-text queries have somewhere to go. The two are
 * independent and use different variables on purpose.
 */
#pragma once

#include <string>

namespace dbal::search {

struct SearchConfig {
    /// False when DBAL_SEARCH_URL is unset or empty -- the daemon then runs
    /// exactly as it did before the search layer existed.
    bool enabled = false;

    /// Base URL with DBAL's own query parameters removed.
    std::string base_url;

    /// Index documents are mirrored into. One index for all entities, with an
    /// "_entity" field to discriminate: entity counts here are in the dozens,
    /// and an index per entity multiplies shard overhead for no gain at this
    /// size.
    std::string index = "dbal_search";

    /// Passed to Elasticsearch as ?refresh=. "true" makes a write visible to
    /// the next search immediately, at a real indexing cost; "false" leaves it
    /// visible within the refresh interval (1s by default). Tests and demos
    /// want true, production usually does not.
    std::string refresh = "false";

    /// Human-readable reason the URL was rejected; empty when it parsed.
    std::string error;
};

/// Parses a DBAL_SEARCH_URL value. Never throws: an unusable URL comes back
/// with enabled=false and error set, so the caller can log it and run without
/// search rather than refusing to start.
SearchConfig parseSearchUrl(const std::string& url);

/// Reads DBAL_SEARCH_URL from the environment and parses it.
SearchConfig searchConfigFromEnv();

} // namespace dbal::search
