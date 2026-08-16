/**
 * @file searching_adapter.hpp
 * @brief Mirrors writes into Elasticsearch so full-text search has an index.
 *
 * Decorator, like CachingAdapter: it wraps whichever adapter the factory built,
 * forwards every read and write to it, and additionally mirrors successful
 * writes into Elasticsearch. The wrapped adapter stays authoritative -- nothing
 * is ever read back from Elasticsearch except through search().
 *
 * This is the *secondary* search layer, not the elasticsearch adapter. Pointing
 * DATABASE_URL at Elasticsearch makes it the primary store; DBAL_SEARCH_URL
 * keeps Postgres (or whatever) primary and maintains a searchable copy.
 *
 * Consistency, stated plainly: mirroring is best-effort and asynchronous to the
 * caller's transaction. A failed mirror is logged and the primary write still
 * succeeds, because refusing a write because a search index was unreachable
 * would make the search layer a availability risk to the whole daemon. So the
 * index can drift, and unlike the read-through cache there is no TTL to bound
 * it -- a stale document persists until that record is next written. Anything
 * needing exactness must read the primary; search results are a discovery aid.
 * Bulk operations mirror nothing at all (see the class comment on updateMany).
 */
#pragma once

#include "dbal/adapters/adapter.hpp"
#include "dbal/search/search_config.hpp"

#include <memory>
#include <string>

namespace dbal::adapters::elasticsearch { class ElasticsearchHttpClient; }

namespace dbal::search {

using Json = dbal::adapters::Json;
template <typename T>
using ListResult = dbal::adapters::ListResult<T>;
using EntitySchema = dbal::adapters::EntitySchema;
using dbal::ListOptions;

class SearchingAdapter : public dbal::adapters::Adapter {
public:
    /// Wraps `inner` in a search layer, or returns nullptr if Elasticsearch is
    /// unusable. Takes `inner` by reference and moves from it only once the
    /// client exists -- same ownership hazard as CachingAdapter::tryCreate,
    /// where a throw after the move would destroy the wrapped adapter and
    /// leave the caller holding nullptr.
    ///
    /// On failure `inner` is untouched. Never throws.
    static std::unique_ptr<dbal::adapters::Adapter> tryCreate(
        std::unique_ptr<dbal::adapters::Adapter>& inner, const SearchConfig& config,
        std::string& error) noexcept;

    ~SearchingAdapter() override;

    /// Full-text search over the mirrored documents for one entity.
    Result<ListResult<Json>> search(const std::string& entityName,
                                     const std::string& query, int limit) override;

    // --- writes: delegate, then mirror -------------------------------------
    Result<Json> create(const std::string& entityName, const Json& data) override;
    Result<Json> update(const std::string& entityName, const std::string& id, const Json& data) override;
    Result<bool> remove(const std::string& entityName, const std::string& id) override;
    Result<Json> upsert(const std::string& entityName, const std::string& uniqueField,
                        const Json& uniqueValue, const Json& createData, const Json& updateData) override;

    // --- pass-through ------------------------------------------------------
    Result<Json> read(const std::string& entityName, const std::string& id) override;
    Result<Json> readIncludingSensitive(const std::string& entityName, const std::string& id) override;
    Result<ListResult<Json>> list(const std::string& entityName, const ListOptions& options) override;
    Result<Json> findFirst(const std::string& entityName, const Json& filter) override;
    Result<Json> findByField(const std::string& entityName, const std::string& field, const Json& value) override;
    Result<int> createMany(const std::string& entityName, const std::vector<Json>& records) override;
    Result<int> updateMany(const std::string& entityName, const Json& filter, const Json& data) override;
    Result<int> deleteMany(const std::string& entityName, const Json& filter) override;
    Result<std::vector<std::string>> getAvailableEntities() override;
    Result<EntitySchema> getEntitySchema(const std::string& entityName) override;
    void close() override;

    bool supportsNativeTransactions() const override;
    Result<bool> beginTransaction() override;
    Result<bool> commitTransaction() override;
    Result<bool> rollbackTransaction() override;

private:
    SearchingAdapter(std::unique_ptr<dbal::adapters::Adapter> inner,
                     std::unique_ptr<dbal::adapters::elasticsearch::ElasticsearchHttpClient> es,
                     std::string index) noexcept;

    /// Indexes one record. Silent on failure -- the primary write already
    /// succeeded and the caller has been told so.
    void mirrorRecord(const std::string& entityName, const Json& record) noexcept;

    /// Removes one document. Silent on failure.
    void mirrorDelete(const std::string& entityName, const std::string& id) noexcept;

    /// Documents from every entity share one index, so the id must be unique
    /// across entities: "{entity}:{id}".
    static std::string documentId(const std::string& entityName, const std::string& id);

    std::unique_ptr<dbal::adapters::Adapter> inner_;
    std::unique_ptr<dbal::adapters::elasticsearch::ElasticsearchHttpClient> es_;
    std::string index_;
};

} // namespace dbal::search
