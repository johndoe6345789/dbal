#include "dbal/core/loaders/relation_parser.hpp"
#include "dbal/core/entity_loader.hpp"

namespace dbal {
namespace core {
namespace loaders {

EntityIndex RelationParser::parseIndex(const nlohmann::json& indexNode) {
    EntityIndex index;
    index.fields = parseIndexFields(indexNode);
    index.unique = indexNode.value("unique", false);
    if (indexNode.contains("name"))
        index.name = indexNode["name"].get<std::string>();
    return index;
}

EntitySchema::ACL RelationParser::parseACL(const nlohmann::json& aclNode) {
    EntitySchema::ACL acl;
    if (aclNode.contains("create")) acl.create = parseACLOperation(aclNode["create"]);
    if (aclNode.contains("read"))   acl.read   = parseACLOperation(aclNode["read"]);
    if (aclNode.contains("update")) acl.update = parseACLOperation(aclNode["update"]);
    if (aclNode.contains("delete")) acl.del    = parseACLOperation(aclNode["delete"]);
    return acl;
}

std::vector<std::string> RelationParser::parseIndexFields(const nlohmann::json& indexNode) {
    std::vector<std::string> fields;
    if (indexNode.contains("fields")) {
        for (const auto& f : indexNode["fields"])
            fields.push_back(f.get<std::string>());
    }
    return fields;
}

std::map<std::string, bool> RelationParser::parseACLOperation(const nlohmann::json& operationNode) {
    std::map<std::string, bool> permissions;

    // Only boolean predicates are representable in this map. The schemas also
    // carry role lists ("role": ["admin","god"]), single roles
    // ("role": "user") and ownership templates
    // ("uploaderId": "{{ currentUserId }}") -- none of which a
    // map<string,bool> can express, and none of which anything currently
    // enforces. Skipping them preserves the flags that ARE modelled, above
    // all "system": true, the single predicate SchemaAclRegistry reads.
    //
    // Calling get<bool>() unconditionally instead threw type_error.302, and
    // the catch sits all the way up in EntitySchemaLoader::loadSchemas -- so
    // one unmodelled value discarded the ENTIRE schema file. Because
    // SchemaAclRegistry fails open on entities it has never heard of, that
    // turned a parse quirk into silently unenforced access control on every
    // system-only entity in the dropped file (EmailAttachment, MediaJob,
    // Notification). Skip what cannot be modelled; never drop the schema.
    auto collectBooleans = [&permissions](const nlohmann::json& conditions) {
        if (!conditions.is_object()) return;
        for (const auto& [key, value] : conditions.items())
            if (value.is_boolean()) permissions[key] = value.get<bool>();
    };

    // An operation is either a single condition object --
    //     "read": { "public": true }
    // -- or an array of alternatives, any one of which grants access:
    //     "read": [ { "authenticated": true }, { "ownerId": "{{ currentUserId }}" } ]
    // Flattening the array is sound for the boolean flags: "system" appears
    // in an alternative only when that alternative is the system-only one,
    // and nothing reads the others.
    if (operationNode.is_array()) {
        for (const auto& alternative : operationNode) collectBooleans(alternative);
    } else {
        collectBooleans(operationNode);
    }

    return permissions;
}

}  // namespace loaders
}  // namespace core
}  // namespace dbal
