/**
 * @file client_init.cpp
 * @brief DBAL Client initialization and lifecycle management.
 *
 * This file contains:
 * - Constructor: Validates config and creates adapter
 * - Destructor: Cleanup resources
 * - close(): Explicit cleanup method
 */
#include "dbal/client.hpp"
#include "dbal/core/adapter_factory.hpp"
#include "dbal/core/connection_validator.hpp"
#include "dbal/core/client_config.hpp"
#include "dbal/core/operation_executor.hpp"
#include "dbal/core/metadata_cache.hpp"
#include "dbal/cache/cache_config.hpp"
#include "dbal/cache/caching_adapter.hpp"
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <utility>

namespace dbal {

Client::Client(const ClientConfig& config) : config_(config) {
    // Validate configuration using ClientConfigManager
    core::ClientConfigManager config_manager(
        config.mode,
        config.adapter,
        config.endpoint,
        config.database_url,
        config.sandbox_enabled
    );

    // Create adapter using factory
    adapter_ = core::AdapterFactory::createFromUrl(config.database_url);

    // Optionally put a read-through cache in front of it. Everything below is
    // best-effort by design: the cache is a latency optimisation over an
    // adapter that is already correct, so no failure here is worth refusing to
    // start for. A misconfigured or unreachable Redis leaves a daemon that
    // behaves exactly as it did before caching existed, and says why.
    auto cache_config = cache::cacheConfigFromEnv();
    if (!cache_config.error.empty()) {
        spdlog::warn("[cache] ignoring DBAL_CACHE_URL: {}", cache_config.error);
    } else if (cache_config.enabled) {
        // tryCreate leaves adapter_ alone unless it succeeds, so the failure
        // path here really is "carry on uncached" rather than "carry on with a
        // null adapter".
        std::string cache_error;
        if (auto cached = cache::CachingAdapter::tryCreate(adapter_, cache_config, cache_error)) {
            adapter_ = std::move(cached);
            spdlog::info("[cache] read-through cache enabled (ttl={}s)", cache_config.ttl_seconds);
        } else {
            spdlog::warn("[cache] disabled — could not initialise Redis: {}", cache_error);
        }
    }
}

Client::~Client() {
    close();
}

void Client::close() {
    // For in-memory implementation, optionally clear store.
}

} // namespace dbal
