/**
 * @file caching_adapter.cpp
 */
#include "dbal/cache/caching_adapter.hpp"
#include "dbal/cache/cache_key.hpp"

#include <sw/redis++/redis++.h>
#include <spdlog/spdlog.h>

#include <stdexcept>
#include <utility>
#include <vector>

namespace dbal::cache {


namespace {

/// Pulls the record id out of a write result so the right key can be dropped.
/// Adapters return the stored record, which carries "id" -- but a failed or
/// oddly-shaped result must not throw here, since invalidation runs on the
/// success path of a write the caller already considers done.
std::string idFromRecord(const Json& record) {
    if (!record.is_object()) return {};
    auto it = record.find("id");
    if (it == record.end()) return {};
    if (it->is_string()) return it->get<std::string>();
    if (it->is_number_integer()) return std::to_string(it->get<long long>());
    return {};
}

} // namespace

CachingAdapter::CachingAdapter(std::unique_ptr<dbal::adapters::Adapter> inner,
                                std::unique_ptr<sw::redis::Redis> redis, int ttl_seconds) noexcept
    : inner_(std::move(inner)), redis_(std::move(redis)), ttl_seconds_(ttl_seconds) {}

std::unique_ptr<dbal::adapters::Adapter> CachingAdapter::tryCreate(
    std::unique_ptr<dbal::adapters::Adapter>& inner, const CacheConfig& config,
    std::string& error) noexcept {

    if (!inner) { error = "no adapter to wrap"; return nullptr; }
    if (!config.enabled) { error = "cache is not enabled"; return nullptr; }

    std::unique_ptr<sw::redis::Redis> redis;
    try {
        // Everything that can fail happens here, while `inner` is still the
        // caller's. redis-plus-plus parses the URL eagerly and connects
        // lazily, so a malformed URL is caught now and an unreachable server
        // surfaces on first use, where it degrades to a database read.
        redis = std::make_unique<sw::redis::Redis>(config.redis_url);
    } catch (const std::exception& e) {
        error = e.what();
        return nullptr;   // inner untouched
    }

    // Past this point nothing throws, so the move is safe.
    return std::unique_ptr<dbal::adapters::Adapter>(
        new CachingAdapter(std::move(inner), std::move(redis), config.ttl_seconds));
}

CachingAdapter::~CachingAdapter() = default;

// ---------------------------------------------------------------------------
// cached read
// ---------------------------------------------------------------------------

Result<Json> CachingAdapter::read(const std::string& entityName, const std::string& id) {
    const std::string key = recordKey(entityName, id);

    try {
        if (auto hit = redis_->get(key)) {
            // A corrupt entry must not poison the read: fall through to the
            // database and let the SETEX below overwrite it.
            Json parsed = Json::parse(*hit, nullptr, /*allow_exceptions=*/false);
            if (!parsed.is_discarded()) return parsed;
            spdlog::warn("[cache] discarding unparseable entry for {}", key);
        }
    } catch (const std::exception& e) {
        spdlog::warn("[cache] read failed for {}: {} -- serving from database", key, e.what());
    }

    auto result = inner_->read(entityName, id);
    if (!result.isOk()) return result;

    try {
        redis_->setex(key, ttl_seconds_, result.value().dump());
    } catch (const std::exception& e) {
        // The caller already has its answer; a failed backfill only costs the
        // next reader a miss.
        spdlog::warn("[cache] backfill failed for {}: {}", key, e.what());
    }
    return result;
}

// ---------------------------------------------------------------------------
// writes -- delegate first, invalidate only what actually changed
// ---------------------------------------------------------------------------

Result<Json> CachingAdapter::create(const std::string& entityName, const Json& data) {
    auto result = inner_->create(entityName, data);
    // A create cannot have a cached copy under its own id yet, but an earlier
    // read of a since-deleted row with the same id could still be cached.
    if (result.isOk()) invalidateRecord(entityName, idFromRecord(result.value()));
    return result;
}

Result<Json> CachingAdapter::update(const std::string& entityName, const std::string& id, const Json& data) {
    auto result = inner_->update(entityName, id, data);
    // Invalidate rather than write the new value through: the adapter may have
    // applied defaults, triggers or column coercion, so the authoritative row
    // is whatever the next read returns, not what was passed in here.
    if (result.isOk()) invalidateRecord(entityName, id);
    return result;
}

Result<bool> CachingAdapter::remove(const std::string& entityName, const std::string& id) {
    auto result = inner_->remove(entityName, id);
    if (result.isOk()) invalidateRecord(entityName, id);
    return result;
}

Result<Json> CachingAdapter::upsert(const std::string& entityName, const std::string& uniqueField,
                                     const Json& uniqueValue, const Json& createData, const Json& updateData) {
    auto result = inner_->upsert(entityName, uniqueField, uniqueValue, createData, updateData);
    if (result.isOk()) invalidateRecord(entityName, idFromRecord(result.value()));
    return result;
}

Result<int> CachingAdapter::createMany(const std::string& entityName, const std::vector<Json>& records) {
    auto result = inner_->createMany(entityName, records);
    if (result.isOk()) invalidateEntity(entityName);
    return result;
}

Result<int> CachingAdapter::updateMany(const std::string& entityName, const Json& filter, const Json& data) {
    auto result = inner_->updateMany(entityName, filter, data);
    // The filter names rows, not ids, so which keys went stale is unknown
    // without re-running the query. Drop the entity's cached records instead.
    if (result.isOk()) invalidateEntity(entityName);
    return result;
}

Result<int> CachingAdapter::deleteMany(const std::string& entityName, const Json& filter) {
    auto result = inner_->deleteMany(entityName, filter);
    if (result.isOk()) invalidateEntity(entityName);
    return result;
}

// ---------------------------------------------------------------------------
// invalidation
// ---------------------------------------------------------------------------

void CachingAdapter::invalidateRecord(const std::string& entityName, const std::string& id) noexcept {
    if (id.empty()) return;
    try {
        redis_->del(recordKey(entityName, id));
    } catch (const std::exception& e) {
        // Worst case the entry survives until its TTL. Logged at warn because
        // it is a real (bounded) staleness window, not a routine miss.
        spdlog::warn("[cache] invalidate failed for {}:{}: {}", entityName, id, e.what());
    }
}

void CachingAdapter::invalidateEntity(const std::string& entityName) noexcept {
    const std::string pattern = entityScanPattern(entityName);
    try {
        // SCAN rather than KEYS: KEYS blocks the server for the whole keyspace,
        // and this runs on a live request path. UNLINK rather than DEL so the
        // reclaim happens off the main thread.
        long long cursor = 0;
        std::vector<std::string> batch;
        do {
            batch.clear();
            cursor = redis_->scan(cursor, pattern, 500, std::back_inserter(batch));
            if (!batch.empty()) redis_->unlink(batch.begin(), batch.end());
        } while (cursor != 0);
    } catch (const std::exception& e) {
        spdlog::warn("[cache] bulk invalidate failed for {}: {} -- entries remain until TTL",
                     entityName, e.what());
    }
}

// ---------------------------------------------------------------------------
// pass-through
// ---------------------------------------------------------------------------

Result<ListResult<Json>> CachingAdapter::search(const std::string& entityName,
                                                 const std::string& query, int limit) {
    // Not cached: results depend on the query text, and the search layer
    // underneath already owns their freshness.
    return inner_->search(entityName, query, limit);
}

Result<Json> CachingAdapter::readIncludingSensitive(const std::string& entityName, const std::string& id) {
    // Never cached: this path returns fields read() redacts.
    return inner_->readIncludingSensitive(entityName, id);
}

Result<ListResult<Json>> CachingAdapter::list(
    const std::string& entityName, const ListOptions& options) {
    return inner_->list(entityName, options);
}

Result<Json> CachingAdapter::findFirst(const std::string& entityName, const Json& filter) {
    return inner_->findFirst(entityName, filter);
}

Result<Json> CachingAdapter::findByField(const std::string& entityName, const std::string& field,
                                          const Json& value) {
    return inner_->findByField(entityName, field, value);
}

Result<std::vector<std::string>> CachingAdapter::getAvailableEntities() {
    return inner_->getAvailableEntities();
}

Result<EntitySchema> CachingAdapter::getEntitySchema(const std::string& entityName) {
    return inner_->getEntitySchema(entityName);
}

void CachingAdapter::close() {
    inner_->close();
}

bool CachingAdapter::supportsNativeTransactions() const { return inner_->supportsNativeTransactions(); }
Result<bool> CachingAdapter::beginTransaction()    { return inner_->beginTransaction(); }
Result<bool> CachingAdapter::commitTransaction()   { return inner_->commitTransaction(); }
Result<bool> CachingAdapter::rollbackTransaction() { return inner_->rollbackTransaction(); }

} // namespace dbal::cache
