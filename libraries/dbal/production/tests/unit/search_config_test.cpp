/**
 * @file search_config_test.cpp
 * @brief DBAL_SEARCH_URL parsing.
 *
 * Pure function, so no Elasticsearch is needed. The index name matters most
 * here: it is interpolated straight into request paths, so anything that could
 * change the path has to be refused at parse time.
 */
#include "dbal/search/search_config.hpp"

#include <gtest/gtest.h>
#include <string>

using namespace dbal::search;

TEST(SearchConfig, UnsetIsDisabledAndNotAnError) {
    auto cfg = parseSearchUrl("");
    EXPECT_FALSE(cfg.enabled);
    EXPECT_TRUE(cfg.error.empty());
}

TEST(SearchConfig, ParsesUrlIndexAndRefresh) {
    auto cfg = parseSearchUrl("http://localhost:9200?index=dbal_search&refresh=true");
    ASSERT_TRUE(cfg.enabled) << cfg.error;
    EXPECT_EQ(cfg.base_url, "http://localhost:9200");
    EXPECT_EQ(cfg.index, "dbal_search");
    EXPECT_EQ(cfg.refresh, "true");
}

TEST(SearchConfig, Defaults) {
    auto cfg = parseSearchUrl("http://es:9200");
    ASSERT_TRUE(cfg.enabled) << cfg.error;
    EXPECT_EQ(cfg.index, "dbal_search");
    EXPECT_EQ(cfg.refresh, "false") << "refresh=true costs real indexing throughput; opt in";
}

TEST(SearchConfig, StripsQueryAndTrailingSlash) {
    auto cfg = parseSearchUrl("http://es:9200/?index=x");
    ASSERT_TRUE(cfg.enabled) << cfg.error;
    EXPECT_EQ(cfg.base_url, "http://es:9200") << "else paths become //index/_doc";
}

TEST(SearchConfig, RejectsNonHttpScheme) {
    EXPECT_FALSE(parseSearchUrl("redis://es:9200").enabled);
    EXPECT_FALSE(parseSearchUrl("es://es:9200").enabled);
}

TEST(SearchConfig, AcceptsHttps) {
    EXPECT_TRUE(parseSearchUrl("https://secure:9243?index=prod").enabled);
}

TEST(SearchConfig, RejectsIndexNamesThatCouldAlterTheRequestPath) {
    // These are interpolated into "/{index}/_doc/{id}".
    for (const std::string bad : {"a/b", "q?x", "has space", "UPPER", "_leading", "-leading", ".leading", ""}) {
        auto cfg = parseSearchUrl("http://es:9200?index=" + bad);
        EXPECT_FALSE(cfg.enabled) << "accepted index name: '" << bad << "'";
    }
}

TEST(SearchConfig, RejectsUnknownRefreshValue) {
    EXPECT_FALSE(parseSearchUrl("http://es:9200?refresh=maybe").enabled);
}

TEST(SearchConfig, AcceptsWaitForRefresh) {
    auto cfg = parseSearchUrl("http://es:9200?refresh=wait_for");
    ASSERT_TRUE(cfg.enabled) << cfg.error;
    EXPECT_EQ(cfg.refresh, "wait_for");
}

TEST(SearchConfig, IgnoresUnknownParameters) {
    auto cfg = parseSearchUrl("http://es:9200?future_option=1&index=ok9");
    ASSERT_TRUE(cfg.enabled) << cfg.error;
    EXPECT_EQ(cfg.index, "ok9");
}
