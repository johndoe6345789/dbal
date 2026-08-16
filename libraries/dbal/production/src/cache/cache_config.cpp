/**
 * @file cache_config.cpp
 */
#include "dbal/cache/cache_config.hpp"

#include <cstdlib>
#include <string>

namespace dbal::cache {

namespace {

/// Splits "k=v&k2=v2" into calls to fn(key, value). Empty pairs are skipped
/// so a trailing or doubled '&' is not an error.
template <typename Fn>
void forEachQueryParam(const std::string& query, Fn&& fn) {
    std::size_t pos = 0;
    while (pos < query.size()) {
        std::size_t amp = query.find('&', pos);
        if (amp == std::string::npos) amp = query.size();
        std::string pair = query.substr(pos, amp - pos);
        pos = amp + 1;
        if (pair.empty()) continue;
        std::size_t eq = pair.find('=');
        if (eq == std::string::npos) continue;
        fn(pair.substr(0, eq), pair.substr(eq + 1));
    }
}

} // namespace

CacheConfig parseCacheUrl(const std::string& url) {
    CacheConfig cfg;
    if (url.empty()) return cfg;  // not configured; not an error

    // Only redis is supported. Rejecting anything else by scheme is clearer
    // than letting redis-plus-plus fail later with a connection error that
    // looks like the server is down.
    if (url.rfind("redis://", 0) != 0 && url.rfind("rediss://", 0) != 0) {
        cfg.error = "DBAL_CACHE_URL must begin with redis:// or rediss:// (got: " + url + ")";
        return cfg;
    }

    std::size_t q = url.find('?');
    cfg.redis_url = url.substr(0, q == std::string::npos ? url.size() : q);

    bool bad = false;
    if (q != std::string::npos) {
        forEachQueryParam(url.substr(q + 1), [&](const std::string& key, const std::string& value) {
            if (bad) return;
            if (key == "ttl") {
                try {
                    std::size_t consumed = 0;
                    int parsed = std::stoi(value, &consumed);
                    // stoi stops at the first non-digit, so "300abc" would
                    // otherwise silently become 300.
                    if (consumed != value.size() || parsed <= 0) {
                        cfg.error = "DBAL_CACHE_URL ttl must be a positive integer (got: " + value + ")";
                        bad = true;
                        return;
                    }
                    cfg.ttl_seconds = parsed;
                } catch (const std::exception&) {
                    cfg.error = "DBAL_CACHE_URL ttl must be a positive integer (got: " + value + ")";
                    bad = true;
                }
            } else if (key == "pattern") {
                if (value == "read-through") {
                    cfg.pattern = CachePattern::ReadThrough;
                } else {
                    // Named rather than ignored: the docs list several
                    // patterns and only this one exists, so silently treating
                    // write-through as read-through would be a correctness
                    // surprise, not a convenience.
                    cfg.error = "DBAL_CACHE_URL pattern '" + value +
                                "' is not implemented; only read-through is supported";
                    bad = true;
                }
            }
            // Unknown keys are ignored on purpose, so a future option does not
            // stop an older daemon from starting.
        });
    }

    if (bad) {
        cfg.redis_url.clear();
        return cfg;
    }

    cfg.enabled = true;
    return cfg;
}

CacheConfig cacheConfigFromEnv() {
    const char* url = std::getenv("DBAL_CACHE_URL");
    return parseCacheUrl(url ? url : "");
}

} // namespace dbal::cache
