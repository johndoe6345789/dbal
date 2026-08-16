/**
 * @file cache_config.hpp
 * @brief Parses DBAL_CACHE_URL into the pieces the caching layer needs.
 *
 * Format, as documented in .env.example:
 *
 *   redis://localhost:6379/0?ttl=300&pattern=read-through
 *
 * The query string is DBAL's own, not Redis's, so it is stripped before the
 * remainder is handed to RedisConnectionPool -- redis-plus-plus would reject
 * ttl/pattern as unknown connection options.
 */
#pragma once

#include <cstdint>
#include <string>

namespace dbal::cache {

/// How reads and writes interact with the cache.
enum class CachePattern {
    /// Read: cache first, fall through to the database on a miss and backfill.
    /// Write: invalidate. The only pattern implemented; the database stays
    /// authoritative, so a cold or flushed cache is never a correctness issue.
    ReadThrough,
};

struct CacheConfig {
    /// False when DBAL_CACHE_URL is unset or empty -- the daemon then runs
    /// exactly as it did before caching existed.
    bool enabled = false;

    /// Connection URL with DBAL's query parameters removed.
    std::string redis_url;

    /// Seconds. Caps how long a stale entry can survive a write that did not
    /// go through this process (another replica, psql, a migration).
    int ttl_seconds = 300;

    CachePattern pattern = CachePattern::ReadThrough;

    /// Human-readable reason the URL was rejected; empty when it parsed.
    std::string error;
};

/// Parses a DBAL_CACHE_URL value. Never throws: an unusable URL comes back
/// with enabled=false and error set, so the caller can log it and run
/// uncached rather than refusing to start over a cache.
CacheConfig parseCacheUrl(const std::string& url);

/// Reads DBAL_CACHE_URL from the environment and parses it.
CacheConfig cacheConfigFromEnv();

} // namespace dbal::cache
