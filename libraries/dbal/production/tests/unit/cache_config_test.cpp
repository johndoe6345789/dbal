/**
 * @file cache_config_test.cpp
 * @brief DBAL_CACHE_URL parsing and cache key naming.
 *
 * Both are pure functions, so they are testable without a Redis server -- the
 * parts of the caching layer that need one are covered by the fact that every
 * Redis call falls through to the database on failure.
 */
#include "dbal/cache/cache_config.hpp"
#include "dbal/cache/cache_key.hpp"

#include <gtest/gtest.h>

#include <regex>
#include <string>

using namespace dbal::cache;

// ---------------------------------------------------------------------------
// parseCacheUrl
// ---------------------------------------------------------------------------

TEST(CacheConfig, UnsetIsDisabledAndNotAnError) {
    auto cfg = parseCacheUrl("");
    EXPECT_FALSE(cfg.enabled);
    EXPECT_TRUE(cfg.error.empty()) << "an unset cache is the default, not a misconfiguration";
}

TEST(CacheConfig, ParsesUrlTtlAndPattern) {
    auto cfg = parseCacheUrl("redis://redis:6379/0?ttl=300&pattern=read-through");
    ASSERT_TRUE(cfg.enabled) << cfg.error;
    EXPECT_EQ(cfg.redis_url, "redis://redis:6379/0");
    EXPECT_EQ(cfg.ttl_seconds, 300);
    EXPECT_EQ(cfg.pattern, CachePattern::ReadThrough);
}

TEST(CacheConfig, StripsQueryStringFromRedisUrl) {
    // ttl/pattern are DBAL's parameters, not Redis's; redis-plus-plus rejects
    // unknown connection options, so they must not reach it.
    auto cfg = parseCacheUrl("redis://h:6379/2?ttl=15");
    ASSERT_TRUE(cfg.enabled) << cfg.error;
    EXPECT_EQ(cfg.redis_url, "redis://h:6379/2");
    EXPECT_EQ(cfg.redis_url.find('?'), std::string::npos);
}

TEST(CacheConfig, DefaultsTtlWhenAbsent) {
    auto cfg = parseCacheUrl("redis://h:6379/0");
    ASSERT_TRUE(cfg.enabled) << cfg.error;
    EXPECT_EQ(cfg.ttl_seconds, 300);
}

TEST(CacheConfig, RejectsNonRedisScheme) {
    auto cfg = parseCacheUrl("postgresql://h/db");
    EXPECT_FALSE(cfg.enabled);
    EXPECT_FALSE(cfg.error.empty());
}

TEST(CacheConfig, AcceptsTlsScheme) {
    auto cfg = parseCacheUrl("rediss://h:6380/1");
    EXPECT_TRUE(cfg.enabled) << cfg.error;
}

TEST(CacheConfig, RejectsTrailingJunkInTtl) {
    // std::stoi alone would stop at the first non-digit and silently accept
    // this as 300.
    auto cfg = parseCacheUrl("redis://h:6379/0?ttl=300abc");
    EXPECT_FALSE(cfg.enabled);
    EXPECT_FALSE(cfg.error.empty());
}

TEST(CacheConfig, RejectsNonPositiveTtl) {
    EXPECT_FALSE(parseCacheUrl("redis://h:6379/0?ttl=0").enabled);
    EXPECT_FALSE(parseCacheUrl("redis://h:6379/0?ttl=-5").enabled);
}

TEST(CacheConfig, RejectsUnimplementedPatternLoudly) {
    // .env.example and CLAUDE.md list several patterns; only read-through
    // exists. Treating write-through as read-through would silently change
    // write semantics, so it is refused by name.
    auto cfg = parseCacheUrl("redis://h:6379/0?pattern=write-through");
    EXPECT_FALSE(cfg.enabled);
    EXPECT_NE(cfg.error.find("not implemented"), std::string::npos);
}

TEST(CacheConfig, IgnoresUnknownParameters) {
    // So a URL written for a newer daemon does not stop an older one starting.
    auto cfg = parseCacheUrl("redis://h:6379/0?future_option=x&ttl=60");
    ASSERT_TRUE(cfg.enabled) << cfg.error;
    EXPECT_EQ(cfg.ttl_seconds, 60);
}

TEST(CacheConfig, ToleratesSloppySeparators) {
    auto cfg = parseCacheUrl("redis://h:6379/0?&&ttl=5&");
    ASSERT_TRUE(cfg.enabled) << cfg.error;
    EXPECT_EQ(cfg.ttl_seconds, 5);
}

// ---------------------------------------------------------------------------
// cache keys
// ---------------------------------------------------------------------------

namespace {

/// The SCAN glob and the record key are written in different functions; this
/// converts the glob to a regex so a test can assert they still agree. If they
/// drift, bulk invalidation quietly stops matching and stale rows are served
/// until their TTL -- a failure with no error anywhere.
std::regex globToRegex(const std::string& glob) {
    std::string re;
    for (char c : glob) {
        if (c == '*') re += ".*";
        else if (std::string(".^$|()[]{}+?\\").find(c) != std::string::npos) { re += '\\'; re += c; }
        else re += c;
    }
    return std::regex("^" + re + "$");
}

} // namespace

TEST(CacheKey, RecordKeyIsNamespaced) {
    // Must not collide with the redis *adapter*, which stores entities in the
    // same server as authoritative data rather than cached copies.
    EXPECT_EQ(recordKey("User", "god"), "dbal:cache:User:god");
}

TEST(CacheKey, ScanPatternMatchesEveryRecordKeyForThatEntity) {
    const std::regex rx = globToRegex(entityScanPattern("User"));
    for (const std::string id : {"god", "user_god_system", "a:b:c", "", "123"}) {
        EXPECT_TRUE(std::regex_match(recordKey("User", id), rx))
            << "bulk invalidation would miss id '" << id << "'";
    }
}

TEST(CacheKey, ScanPatternDoesNotMatchPrefixSharingEntity) {
    // "User" must not invalidate "UserProfile".
    const std::regex rx = globToRegex(entityScanPattern("User"));
    EXPECT_FALSE(std::regex_match(recordKey("UserProfile", "x"), rx));
}
