/**
 * @file schema_acl_registry.hpp
 * @brief Loads every entity schema once and answers "is this entity/method
 *        system-only?" (schema.acl.<op>.system == true).
 *
 * Fixes a real pre-existing gap: EntitySchemaLoader::parseACL already parses
 * schema.acl into EntitySchema::ACL, but nothing in the route layer ever
 * reads it — any entity, including internal-only ones, was reachable through
 * the generic /{tenant}/{package}/{entity} routes. This registry is what
 * server_routes.cpp calls to close that gap.
 */
#pragma once

#include "dbal/core/entity_loader.hpp"
#include <map>
#include <string>
#include <vector>

namespace dbal::core {

/**
 * @brief Read-only, load-once-at-startup registry of per-entity ACL flags.
 */
class SchemaAclRegistry {
public:
    /// Loads every *.json schema under @p schemaDir via EntitySchemaLoader.
    explicit SchemaAclRegistry(const std::string& schemaDir);

    /**
     * @brief True if @p entity's ACL marks @p method as system-only.
     * @param method One of "create", "read", "update", "delete".
     *
     * Unknown entities and entities with no acl.<method> block at all are
     * NOT system-only by default (fail open to existing behavior) — this
     * registry only tightens entities that explicitly opt in via schema.acl,
     * matching how Credential.json/Session.json already declare it.
     */
    bool isSystemOnly(const std::string& entity, const std::string& method) const;

    /**
     * @brief Roles allowed to perform @p method on @p entity, or empty.
     *
     * Empty means the schema names no roles for that operation, which is not
     * a restriction -- callers must treat it as "allowed", the same fail-open
     * default isSystemOnly() uses. Twenty entities declare write roles.
     */
    std::vector<std::string> requiredRoles(const std::string& entity,
                                           const std::string& method) const;

    /**
     * @brief True if @p method on @p entity is declared publicly writable.
     *
     * Only an explicit acl.<op>.public == true opts an operation out of
     * requiring a caller. User.create is the one that does, so signing up
     * works without already being signed in.
     */
    bool isPublicWrite(const std::string& entity, const std::string& method) const;

    /**
     * @brief True if @p entity may only be read by an authenticated caller.
     *
     * The read mirror of isPublicWrite. An entity that declares a read ACL
     * without granting `public` has said who may read it, and anonymous is
     * not on the list -- self, admin, role and row_level all mean "somebody
     * in particular". Declaring no read rule at all leaves the entity open,
     * the same fail-open default the predicates above use.
     *
     * This gates *access*, not rows: an authenticated caller still reads the
     * whole collection. Narrowing self/row_level to the caller's own rows is
     * the auth_config filter_by_owner machinery's job, and is unfinished for
     * entities outside the pastebin tenant.
     */
    bool requiresAuthToRead(const std::string& entity) const;

    /** @brief Fields an unauthenticated caller may not set. */
    std::vector<std::string> privilegedFields(const std::string& entity) const;

    /** @brief True if @p role is in requiredRoles(), or nothing is required. */
    bool roleAllowed(const std::string& entity, const std::string& method,
                     const std::string& role) const;

private:
    std::map<std::string, EntitySchema> schemas_;

    static const std::map<std::string, bool>* operationMap(
        const EntitySchema::ACL& acl, const std::string& method);
};

} // namespace dbal::core
