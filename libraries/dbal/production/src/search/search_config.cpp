/**
 * @file search_config.cpp
 */
#include "dbal/search/search_config.hpp"

#include <cstdlib>
#include <string>

namespace dbal::search {

namespace {

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

/// Index names are interpolated straight into request paths, so anything that
/// could change the path or is rejected by Elasticsearch is refused here where
/// the error can name the variable.
bool isValidIndexName(const std::string& name) {
    if (name.empty() || name.size() > 255) return false;
    if (name[0] == '_' || name[0] == '-' || name[0] == '+' || name[0] == '.') return false;
    for (char c : name) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                        c == '_' || c == '-' || c == '.';
        if (!ok) return false;  // rejects uppercase, '/', '?', '#', spaces, ...
    }
    return true;
}

} // namespace

SearchConfig parseSearchUrl(const std::string& url) {
    SearchConfig cfg;
    if (url.empty()) return cfg;  // not configured; not an error

    if (url.rfind("http://", 0) != 0 && url.rfind("https://", 0) != 0) {
        cfg.error = "DBAL_SEARCH_URL must begin with http:// or https:// (got: " + url + ")";
        return cfg;
    }

    std::size_t q = url.find('?');
    cfg.base_url = url.substr(0, q == std::string::npos ? url.size() : q);

    // A trailing slash would produce "http://host:9200//dbal_search/_doc".
    // Elasticsearch tolerates it; the logs are confusing enough without it.
    while (cfg.base_url.size() > 8 && cfg.base_url.back() == '/') cfg.base_url.pop_back();

    bool bad = false;
    if (q != std::string::npos) {
        forEachQueryParam(url.substr(q + 1), [&](const std::string& key, const std::string& value) {
            if (bad) return;
            if (key == "index") {
                if (!isValidIndexName(value)) {
                    cfg.error = "DBAL_SEARCH_URL index '" + value +
                                "' is not a valid Elasticsearch index name "
                                "(lowercase letters, digits, '_', '-', '.'; not starting with _ - + .)";
                    bad = true;
                    return;
                }
                cfg.index = value;
            } else if (key == "refresh") {
                // Elasticsearch also accepts "wait_for"; pass it through.
                if (value == "true" || value == "false" || value == "wait_for") {
                    cfg.refresh = value;
                } else {
                    cfg.error = "DBAL_SEARCH_URL refresh must be true, false or wait_for (got: " + value + ")";
                    bad = true;
                }
            }
            // Unknown keys ignored, so a URL written for a newer daemon does
            // not stop an older one starting.
        });
    }

    if (bad) {
        cfg.base_url.clear();
        return cfg;
    }

    cfg.enabled = true;
    return cfg;
}

SearchConfig searchConfigFromEnv() {
    const char* url = std::getenv("DBAL_SEARCH_URL");
    return parseSearchUrl(url ? url : "");
}

} // namespace dbal::search
