/**
 * @file schema_acl_registry.cpp
 */
#include "schema_acl_registry.hpp"
#include <spdlog/spdlog.h>
#include <algorithm>

namespace dbal::core {

SchemaAclRegistry::SchemaAclRegistry(const std::string& schemaDir) {
    EntitySchemaLoader loader;
    schemas_ = loader.loadSchemas(schemaDir);
    spdlog::info("[schema-acl] Loaded ACL metadata for {} entities from {}",
                 schemas_.size(), schemaDir);
}

const std::map<std::string, bool>* SchemaAclRegistry::operationMap(
    const EntitySchema::ACL& acl, const std::string& method) {
    if (method == "create") return &acl.create;
    if (method == "read")   return &acl.read;
    if (method == "update") return &acl.update;
    if (method == "delete") return &acl.del;
    return nullptr;
}

bool SchemaAclRegistry::isSystemOnly(const std::string& entity, const std::string& method) const {
    auto it = schemas_.find(entity);
    if (it == schemas_.end()) return false;
    if (!it->second.acl.has_value()) return false;

    const auto* opMap = operationMap(*it->second.acl, method);
    if (!opMap) return false;

    auto sysIt = opMap->find("system");
    return sysIt != opMap->end() && sysIt->second;
}

bool SchemaAclRegistry::isPublicWrite(const std::string& entity,
                                      const std::string& method) const {
    auto it = schemas_.find(entity);
    if (it == schemas_.end() || !it->second.acl.has_value()) return false;
    const auto* opMap = operationMap(*it->second.acl, method);
    if (!opMap) return false;
    auto pub = opMap->find("public");
    return pub != opMap->end() && pub->second;
}

std::vector<std::string> SchemaAclRegistry::privilegedFields(
    const std::string& entity) const {
    std::vector<std::string> names;
    auto it = schemas_.find(entity);
    if (it == schemas_.end()) return names;
    for (const auto& field : it->second.fields)
        if (field.privileged) names.push_back(field.name);
    return names;
}

std::vector<std::string> SchemaAclRegistry::requiredRoles(
    const std::string& entity, const std::string& method) const {
    auto it = schemas_.find(entity);
    if (it == schemas_.end() || !it->second.acl.has_value()) return {};
    const auto& roles = it->second.acl->roles;
    auto roleIt = roles.find(method);
    if (roleIt == roles.end()) return {};
    return roleIt->second;
}

bool SchemaAclRegistry::roleAllowed(const std::string& entity, const std::string& method,
                                    const std::string& role) const {
    const auto allowed = requiredRoles(entity, method);
    // Nothing declared is not a restriction -- same fail-open default as
    // isSystemOnly, so an entity nobody has written rules for keeps working.
    if (allowed.empty()) return true;
    return std::find(allowed.begin(), allowed.end(), role) != allowed.end();
}

} // namespace dbal::core
