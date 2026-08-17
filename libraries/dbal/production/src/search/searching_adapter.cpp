/**
 * @file searching_adapter.cpp
 */
#include "dbal/search/searching_adapter.hpp"
#include "../adapters/elasticsearch/elasticsearch_http_client.hpp"

#include <spdlog/spdlog.h>

#include <utility>

namespace dbal::search {

using dbal::adapters::elasticsearch::ElasticsearchHttpClient;

namespace {

std::string idFromRecord(const Json& record) {
    if (!record.is_object()) return {};
    auto it = record.find("id");
    if (it == record.end()) return {};
    if (it->is_string()) return it->get<std::string>();
    if (it->is_number_integer()) return std::to_string(it->get<long long>());
    return {};
}

} // namespace

SearchingAdapter::SearchingAdapter(std::unique_ptr<dbal::adapters::Adapter> inner,
                                    std::unique_ptr<ElasticsearchHttpClient> es,
                                    std::string index) noexcept
    : inner_(std::move(inner)), es_(std::move(es)), index_(std::move(index)) {}

SearchingAdapter::~SearchingAdapter() = default;

std::unique_ptr<dbal::adapters::Adapter> SearchingAdapter::tryCreate(
    std::unique_ptr<dbal::adapters::Adapter>& inner, const SearchConfig& config,
    std::string& error) noexcept {

    if (!inner) { error = "no adapter to wrap"; return nullptr; }
    if (!config.enabled) { error = "search is not enabled"; return nullptr; }

    std::unique_ptr<ElasticsearchHttpClient> es;
    try {
        // Constructed while `inner` is still the caller's, so a throw here
        // cannot destroy the wrapped adapter. verify_certs is on: a search
        // index carries the same records as the database.
        es = std::make_unique<ElasticsearchHttpClient>(config.base_url, config.refresh, true);
    } catch (const std::exception& e) {
        error = e.what();
        return nullptr;   // inner untouched
    }

    // Deliberately does not probe the server. Elasticsearch is routinely
    // slower to accept connections than the daemon is to start, and refusing
    // to enable search because it was not up yet would be worse than mirroring
    // failures that log and retry on the next write.
    return std::unique_ptr<dbal::adapters::Adapter>(
        new SearchingAdapter(std::move(inner), std::move(es), config.index));
}

std::string SearchingAdapter::documentId(const std::string& entityName, const std::string& id) {
    return entityName + ":" + id;
}

// ---------------------------------------------------------------------------
// search
// ---------------------------------------------------------------------------

Result<ListResult<Json>> SearchingAdapter::search(const std::string& entityName,
                                                   const std::string& query, int limit) {
    if (limit <= 0) limit = 20;
    if (limit > 1000) limit = 1000;   // a page size, not an export

    // Filter by entity and match across all mirrored fields. The filter clause
    // does not contribute to scoring, so relevance ordering is decided purely
    // by the text match.
    //
    // _entity.keyword, not _entity: dynamic mapping gives every mirrored
    // string field a `text` type (analyzed -- lowercased by the standard
    // analyzer) plus a `.keyword` sub-field (exact, unanalyzed). A `term`
    // query against the analyzed `_entity` field compares the literal
    // PascalCase entity name against lowercased tokens and can never match,
    // so search silently returned zero hits for every entity, every query,
    // regardless of DBAL_SEARCH_URL being configured and mirroring working
    // correctly -- verified live: a fresh write landed in the ES index
    // (_count incremented) but was still unfindable via this endpoint until
    // the query targeted .keyword.
    Json body = {
        {"size", limit},
        {"query", {
            {"bool", {
                {"filter", Json::array({ Json{{"term", {{"_entity.keyword", entityName}}}} })},
                {"must", Json::array({ Json{{"multi_match", {
                    {"query", query},
                    {"lenient", true}   // don't error on numeric/date fields
                }}} })}
            }}
        }}
    };

    auto response = es_->post("/" + index_ + "/_search", body, /*include_refresh=*/false);
    if (!response.isOk()) return response.error();

    // Unlike every other path here, a failure is returned rather than
    // swallowed: the caller explicitly asked to search, so silently answering
    // "no results" would be a lie about the data rather than a degraded read.
    ListResult<Json> out;
    const Json& json = response.value();
    auto hits = json.find("hits");
    if (hits == json.end()) return out;

    auto total = hits->find("total");
    if (total != hits->end()) {
        auto value = total->find("value");
        if (value != total->end() && value->is_number_integer()) out.total = value->get<int>();
    }

    auto inner_hits = hits->find("hits");
    if (inner_hits != hits->end() && inner_hits->is_array()) {
        for (const auto& hit : *inner_hits) {
            auto source = hit.find("_source");
            if (source == hit.end()) continue;
            Json record = *source;
            // Bookkeeping added at mirror time; not part of the record.
            record.erase("_entity");
            out.items.push_back(std::move(record));
        }
    }
    out.limit = limit;
    return out;
}

// ---------------------------------------------------------------------------
// writes -- delegate first, mirror only on success
// ---------------------------------------------------------------------------

Result<Json> SearchingAdapter::create(const std::string& entityName, const Json& data) {
    auto result = inner_->create(entityName, data);
    if (result.isOk()) mirrorRecord(entityName, result.value());
    return result;
}

Result<Json> SearchingAdapter::update(const std::string& entityName, const std::string& id,
                                       const Json& data) {
    auto result = inner_->update(entityName, id, data);
    // Mirror what the adapter stored, not what was passed in: defaults,
    // triggers and coercion mean the two can differ, and the index should
    // reflect the row.
    if (result.isOk()) mirrorRecord(entityName, result.value());
    return result;
}

Result<bool> SearchingAdapter::remove(const std::string& entityName, const std::string& id) {
    auto result = inner_->remove(entityName, id);
    if (result.isOk()) mirrorDelete(entityName, id);
    return result;
}

Result<Json> SearchingAdapter::upsert(const std::string& entityName, const std::string& uniqueField,
                                       const Json& uniqueValue, const Json& createData,
                                       const Json& updateData) {
    auto result = inner_->upsert(entityName, uniqueField, uniqueValue, createData, updateData);
    if (result.isOk()) mirrorRecord(entityName, result.value());
    return result;
}

// ---------------------------------------------------------------------------
// mirroring
// ---------------------------------------------------------------------------

void SearchingAdapter::mirrorRecord(const std::string& entityName, const Json& record) noexcept {
    try {
        const std::string id = idFromRecord(record);
        if (id.empty()) {
            spdlog::warn("[search] not mirroring {} record without an id", entityName);
            return;
        }
        Json doc = record;
        // One index for all entities, so every document carries the entity it
        // came from; search() filters on it.
        doc["_entity"] = entityName;
        auto result = es_->put("/" + index_ + "/_doc/" + documentId(entityName, id), doc,
                               /*include_refresh=*/true);
        if (!result.isOk()) {
            spdlog::warn("[search] mirror failed for {}:{} — index is now stale for this record: {}",
                         entityName, id, result.error().what());
        }
    } catch (const std::exception& e) {
        spdlog::warn("[search] mirror threw for {}: {}", entityName, e.what());
    }
}

void SearchingAdapter::mirrorDelete(const std::string& entityName, const std::string& id) noexcept {
    try {
        auto result = es_->deleteRequest("/" + index_ + "/_doc/" + documentId(entityName, id),
                                         /*include_refresh=*/true);
        // A 404 means the document was never mirrored, which is the normal
        // state for anything written before search was switched on.
        if (!result.isOk()) {
            spdlog::debug("[search] delete mirror for {}:{}: {}",
                          entityName, id, result.error().what());
        }
    } catch (const std::exception& e) {
        spdlog::warn("[search] delete mirror threw for {}:{}: {}", entityName, id, e.what());
    }
}

// ---------------------------------------------------------------------------
// pass-through
// ---------------------------------------------------------------------------

Result<Json> SearchingAdapter::read(const std::string& entityName, const std::string& id) {
    return inner_->read(entityName, id);
}

Result<Json> SearchingAdapter::readIncludingSensitive(const std::string& entityName,
                                                       const std::string& id) {
    return inner_->readIncludingSensitive(entityName, id);
}

Result<ListResult<Json>> SearchingAdapter::list(const std::string& entityName,
                                                 const ListOptions& options) {
    return inner_->list(entityName, options);
}

Result<Json> SearchingAdapter::findFirst(const std::string& entityName, const Json& filter) {
    return inner_->findFirst(entityName, filter);
}

Result<Json> SearchingAdapter::findByField(const std::string& entityName, const std::string& field,
                                            const Json& value) {
    return inner_->findByField(entityName, field, value);
}

// Bulk operations are not mirrored. createMany/updateMany/deleteMany return
// counts rather than records, so there is nothing to index without re-reading
// the affected rows -- and updateMany/deleteMany take a filter, so which rows
// those are is unknown here. Mirroring them would mean issuing a query per
// bulk call on the write path. Left out deliberately: after a bulk write the
// index is stale for those records until each is next written individually,
// which is the same reindex problem any secondary index has and is better
// solved by an explicit reindex than by guessing here.
Result<int> SearchingAdapter::createMany(const std::string& entityName,
                                          const std::vector<Json>& records) {
    return inner_->createMany(entityName, records);
}

Result<int> SearchingAdapter::updateMany(const std::string& entityName, const Json& filter,
                                          const Json& data) {
    return inner_->updateMany(entityName, filter, data);
}

Result<int> SearchingAdapter::deleteMany(const std::string& entityName, const Json& filter) {
    return inner_->deleteMany(entityName, filter);
}

Result<std::vector<std::string>> SearchingAdapter::getAvailableEntities() {
    return inner_->getAvailableEntities();
}

Result<EntitySchema> SearchingAdapter::getEntitySchema(const std::string& entityName) {
    return inner_->getEntitySchema(entityName);
}

void SearchingAdapter::close() { inner_->close(); }

bool SearchingAdapter::supportsNativeTransactions() const { return inner_->supportsNativeTransactions(); }
Result<bool> SearchingAdapter::beginTransaction()    { return inner_->beginTransaction(); }
Result<bool> SearchingAdapter::commitTransaction()   { return inner_->commitTransaction(); }
Result<bool> SearchingAdapter::rollbackTransaction() { return inner_->rollbackTransaction(); }

} // namespace dbal::search
