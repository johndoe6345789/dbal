/**
 * @file pending_authorize_store.hpp
 * @brief Ephemeral, in-process store carrying a validated /authorize request
 *        across the redirect to /oidc/login and back, keyed by an opaque
 *        continuation token instead of raw query-string passthrough (so the
 *        login form can't be used to tamper with client_id/redirect_uri/
 *        code_challenge). Mirrors the existing NonceStore/RateLimiter
 *        in-process-map convention already used elsewhere in this codebase —
 *        this state is UI-flow-transient, not something that needs
 *        multi-backend persistence or to survive a restart.
 */
#pragma once

#include "../../../oidc/oidc_service.hpp"
#include "../../../security/tokens/generate_token.hpp"

#include <chrono>
#include <mutex>
#include <optional>
#include <unordered_map>

namespace dbal::daemon::handlers::oidc {

class PendingAuthorizeStore {
public:
    explicit PendingAuthorizeStore(int ttlSeconds = 300) : ttl_seconds_(ttlSeconds) {}

    std::string store(const dbal::oidc::AuthorizeRequest& req) {
        std::lock_guard<std::mutex> lock(mutex_);
        cleanupLocked();
        std::string token = dbal::security::generate_token();
        entries_[token] = Entry{req, std::chrono::steady_clock::now() +
                                          std::chrono::seconds(ttl_seconds_)};
        return token;
    }

    /// Consumes (removes) the entry — single-use, same principle as auth codes.
    std::optional<dbal::oidc::AuthorizeRequest> take(const std::string& token) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = entries_.find(token);
        if (it == entries_.end()) return std::nullopt;
        auto req = it->second.request;
        auto expiry = it->second.expiry;
        entries_.erase(it);
        if (std::chrono::steady_clock::now() > expiry) return std::nullopt;
        return req;
    }

    /// Reads an entry without consuming it.
    ///
    /// Only for the restart store, which answers "what was this login trying
    /// to do?" after the single-use continuation has already been spent. The
    /// continuation store must keep using take(): re-reading an authorize
    /// request there would let one login form be submitted repeatedly.
    ///
    /// Expired entries are erased here rather than merely reported, so a
    /// restart token cannot be probed indefinitely after it lapses.
    std::optional<dbal::oidc::AuthorizeRequest> peek(const std::string& token) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = entries_.find(token);
        if (it == entries_.end()) return std::nullopt;
        if (std::chrono::steady_clock::now() > it->second.expiry) {
            entries_.erase(it);
            return std::nullopt;
        }
        return it->second.request;
    }

private:
    struct Entry {
        dbal::oidc::AuthorizeRequest request;
        std::chrono::steady_clock::time_point expiry;
    };

    void cleanupLocked() {
        auto now = std::chrono::steady_clock::now();
        for (auto it = entries_.begin(); it != entries_.end();) {
            if (now > it->second.expiry) it = entries_.erase(it);
            else ++it;
        }
    }

    int ttl_seconds_;
    std::mutex mutex_;
    std::unordered_map<std::string, Entry> entries_;
};

} // namespace dbal::daemon::handlers::oidc
