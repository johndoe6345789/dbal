/**
 * @file cache_key.hpp
 * @brief Key naming for the read-through cache.
 *
 * Split out from caching_adapter.cpp so the naming rules can be unit-tested
 * without a Redis server, and so the bulk-invalidation pattern is defined next
 * to the key it has to match -- the two drifting apart would leave stale rows
 * served until their TTL, which is the hardest kind of cache bug to notice.
 */
#pragma once

#include <string>

namespace dbal::cache {

/// Prefix for every key this layer writes. Namespaced so a Redis instance
/// shared with the redis *adapter* (which stores entities as authoritative
/// data) cannot collide with cached copies.
inline constexpr const char* kCacheKeyPrefix = "dbal:cache";

/// Key for one record: dbal:cache:{entity}:{id}
inline std::string recordKey(const std::string& entity, const std::string& id) {
    return std::string(kCacheKeyPrefix) + ":" + entity + ":" + id;
}

/// Glob matching every record key for one entity: dbal:cache:{entity}:*
///
/// Used by bulk writes, which change an unknown set of ids. Paying a SCAN on
/// those rather than carrying a generation counter keeps the read path at a
/// single GET -- bulk writes are rare, reads are not.
inline std::string entityScanPattern(const std::string& entity) {
    return std::string(kCacheKeyPrefix) + ":" + entity + ":*";
}

} // namespace dbal::cache
