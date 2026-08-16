/**
 * @file caching_adapter.hpp
 * @brief Read-through Redis cache in front of any Adapter.
 *
 * Decorator, not an adapter of its own: it wraps whichever adapter the factory
 * built and forwards everything, consulting Redis only on single-record reads.
 * The wrapped adapter stays authoritative, so an empty, stale-by-TTL or
 * entirely unreachable cache can only cost latency, never correctness.
 *
 * What is cached: read(entityName, id). That is the whole surface.
 *
 * What is deliberately NOT cached:
 *   * readIncludingSensitive() -- it returns fields the normal read path
 *     redacts (password hashes, secrets). Those must not be copied into a
 *     second datastore with a different lifetime and eviction policy, and one
 *     that is frequently deployed without auth.
 *   * list/findFirst/findByField -- keyed by query shape rather than id, so a
 *     write would have to invalidate an unknown set of query keys. Getting
 *     that wrong serves stale lists silently; leaving it uncached does not.
 *
 * Every Redis call is wrapped: on any failure the error is logged and the call
 * falls through to the wrapped adapter. A cache outage degrades throughput,
 * it does not take the daemon down.
 */
#pragma once

#include "dbal/adapters/adapter.hpp"
#include "dbal/cache/cache_config.hpp"

#include <memory>
#include <string>

namespace sw::redis { class Redis; }

namespace dbal::cache {

// The adapter interface's vocabulary types live in dbal::adapters (Json,
// ListResult, EntitySchema) and dbal (ListOptions). None of them reach this
// namespace on their own: they are namespace-scope names, not members of
// Adapter, so deriving from it does not bring them into scope.
using Json = dbal::adapters::Json;
template <typename T>
using ListResult = dbal::adapters::ListResult<T>;
using EntitySchema = dbal::adapters::EntitySchema;
using dbal::ListOptions;

class CachingAdapter : public dbal::adapters::Adapter {
public:
    /// Wraps `inner` in a cache, or returns nullptr if Redis is unusable.
    ///
    /// Takes `inner` by reference, and moves from it only after the Redis
    /// connection exists. That ordering is the point of this function: a
    /// constructor cannot offer it. `make_unique<CachingAdapter>(move(a), cfg)`
    /// consumes `a` while building the parameter, before the constructor body
    /// runs -- so a throw inside the body destroys the wrapped adapter during
    /// unwinding and leaves the caller holding a null pointer, turning a
    /// cache-unavailable warning into a crash on the first query.
    ///
    /// On failure `inner` is untouched and still owns the adapter.
    /// Never throws; the reason is reported through `error`.
    static std::unique_ptr<dbal::adapters::Adapter> tryCreate(
        std::unique_ptr<dbal::adapters::Adapter>& inner, const CacheConfig& config,
        std::string& error) noexcept;

    ~CachingAdapter() override;

    // --- cached ------------------------------------------------------------
    Result<Json> read(const std::string& entityName, const std::string& id) override;

    // --- writes: invalidate, then delegate ---------------------------------
    Result<Json> create(const std::string& entityName, const Json& data) override;
    Result<Json> update(const std::string& entityName, const std::string& id, const Json& data) override;
    Result<bool> remove(const std::string& entityName, const std::string& id) override;
    Result<Json> upsert(const std::string& entityName, const std::string& uniqueField,
                        const Json& uniqueValue, const Json& createData, const Json& updateData) override;
    Result<int> createMany(const std::string& entityName, const std::vector<Json>& records) override;
    Result<int> updateMany(const std::string& entityName, const Json& filter, const Json& data) override;
    Result<int> deleteMany(const std::string& entityName, const Json& filter) override;

    // --- pass-through ------------------------------------------------------
    Result<Json> readIncludingSensitive(const std::string& entityName, const std::string& id) override;
    Result<ListResult<Json>> list(const std::string& entityName, const ListOptions& options) override;
    Result<Json> findFirst(const std::string& entityName, const Json& filter) override;
    Result<Json> findByField(const std::string& entityName, const std::string& field, const Json& value) override;
    Result<std::vector<std::string>> getAvailableEntities() override;
    Result<EntitySchema> getEntitySchema(const std::string& entityName) override;
    void close() override;

    bool supportsNativeTransactions() const override;
    Result<bool> beginTransaction() override;
    Result<bool> commitTransaction() override;
    Result<bool> rollbackTransaction() override;

private:
    /// Private: use tryCreate, which sequences the Redis connection before it
    /// takes ownership of `inner`. Nothing in this constructor may throw.
    CachingAdapter(std::unique_ptr<dbal::adapters::Adapter> inner,
                   std::unique_ptr<sw::redis::Redis> redis, int ttl_seconds) noexcept;

    /// Drops one record's cached copy. Silent on failure.
    void invalidateRecord(const std::string& entityName, const std::string& id) noexcept;

    /// Drops every cached record for an entity, via SCAN + UNLINK. Used by
    /// bulk writes, which change an unknown set of ids. Silent on failure.
    void invalidateEntity(const std::string& entityName) noexcept;

    std::unique_ptr<dbal::adapters::Adapter> inner_;
    std::unique_ptr<sw::redis::Redis> redis_;
    int ttl_seconds_;
};

} // namespace dbal::cache
