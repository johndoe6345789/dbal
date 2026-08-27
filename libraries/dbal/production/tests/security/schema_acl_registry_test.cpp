/**
 * @file schema_acl_registry_test.cpp
 * @brief Phase 2 gate: schema.acl.system enforcement must apply to entities
 *        that declare it (Credential) and must NOT affect entities that
 *        don't (regression check against Session and an ordinary entity).
 *
 *        Also covers schema.acl.<op>.role, which twenty entities declare and
 *        nothing read until it was enforced: the role list could not be held
 *        by the map<string,bool> the parser produced, so it was dropped, and
 *        an unauthenticated POST created a StyleRule that says create is
 *        god-only.
 */

#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>

#include "core/schema_acl_registry.hpp"

using dbal::core::SchemaAclRegistry;

namespace {

// Build a tiny throwaway schema directory so this test doesn't depend on
// the real shared/api/schema tree (and stays correct if that tree changes).
class TempSchemaDir {
public:
    TempSchemaDir() {
        dir_ = std::filesystem::temp_directory_path() / "dbal_test_schema_acl_dir";
        std::filesystem::remove_all(dir_);
        std::filesystem::create_directories(dir_);
    }
    ~TempSchemaDir() { std::filesystem::remove_all(dir_); }

    void writeSchema(const std::string& filename, const std::string& json) {
        std::ofstream out(dir_ / filename);
        out << json;
    }

    std::string path() const { return dir_.string(); }

private:
    std::filesystem::path dir_;
};

} // namespace

TEST(SchemaAclRegistry, SystemOnlyEntityIsFlagged) {
    TempSchemaDir dir;
    dir.writeSchema("Credential.json", R"({
        "entity": "Credential",
        "fields": { "username": { "type": "string" }, "passwordHash": { "type": "string", "sensitive": true } },
        "acl": {
            "create": { "system": true },
            "read":   { "system": true },
            "update": { "system": true },
            "delete": { "system": true }
        }
    })");

    SchemaAclRegistry registry(dir.path());
    EXPECT_TRUE(registry.isSystemOnly("Credential", "create"));
    EXPECT_TRUE(registry.isSystemOnly("Credential", "read"));
    EXPECT_TRUE(registry.isSystemOnly("Credential", "update"));
    EXPECT_TRUE(registry.isSystemOnly("Credential", "delete"));
}

TEST(SchemaAclRegistry, OrdinaryEntityIsNotFlagged) {
    TempSchemaDir dir;
    dir.writeSchema("Snippet.json", R"({
        "entity": "Snippet",
        "fields": { "title": { "type": "string" } }
    })");

    SchemaAclRegistry registry(dir.path());
    EXPECT_FALSE(registry.isSystemOnly("Snippet", "create"));
    EXPECT_FALSE(registry.isSystemOnly("Snippet", "read"));
    EXPECT_FALSE(registry.isSystemOnly("Snippet", "update"));
    EXPECT_FALSE(registry.isSystemOnly("Snippet", "delete"));
}

TEST(SchemaAclRegistry, PartialAclOnlyFlagsDeclaredOperations) {
    TempSchemaDir dir;
    // Only "create" is system-only — read/update/delete should stay open,
    // proving this isn't an all-or-nothing per-entity flag.
    dir.writeSchema("Partial.json", R"({
        "entity": "Partial",
        "fields": { "id": { "type": "string" } },
        "acl": { "create": { "system": true } }
    })");

    SchemaAclRegistry registry(dir.path());
    EXPECT_TRUE(registry.isSystemOnly("Partial", "create"));
    EXPECT_FALSE(registry.isSystemOnly("Partial", "read"));
    EXPECT_FALSE(registry.isSystemOnly("Partial", "update"));
    EXPECT_FALSE(registry.isSystemOnly("Partial", "delete"));
}

TEST(SchemaAclRegistry, UnknownEntityIsNotFlagged) {
    TempSchemaDir dir;
    dir.writeSchema("Snippet.json", R"({"entity": "Snippet", "fields": {"title": {"type": "string"}}})");

    SchemaAclRegistry registry(dir.path());
    EXPECT_FALSE(registry.isSystemOnly("DoesNotExist", "read"));
}

// Regression: an entity whose ACL carries values that map<string,bool> cannot
// represent must still load, and its "system": true must still be honoured.
//
// parseACLOperation used to call get<bool>() on every ACL value. Real schemas
// carry role lists, single-role strings and ownership templates, so it threw
// json type_error.302 -- and the catch is up in EntitySchemaLoader::loadSchemas,
// so the whole file was discarded. Because isSystemOnly() fails open on
// entities it never loaded, that silently UNPROTECTED every system-only entity
// in the file: EmailAttachment.create, MediaJob.create/update and
// Notification.create were all reachable through the generic CRUD routes.
TEST(SchemaAclRegistry, NonBooleanAclValuesDoNotDropTheSchema) {
    TempSchemaDir dir;
    dir.writeSchema("MediaJob.json", R"({
        "entity": "MediaJob",
        "fields": { "id": { "type": "string" } },
        "acl": {
            "create": { "system": true },
            "update": { "system": true, "role": ["admin", "god"] },
            "read":   { "role": "user" },
            "delete": { "ownerId": "{{ currentUserId }}" }
        }
    })");

    SchemaAclRegistry registry(dir.path());
    // The entity loaded at all -- previously the whole file was dropped.
    EXPECT_TRUE(registry.isSystemOnly("MediaJob", "create"));
    // "system": true survives alongside a sibling role list.
    EXPECT_TRUE(registry.isSystemOnly("MediaJob", "update"));
    // Unmodellable predicates are skipped, not promoted to system-only.
    EXPECT_FALSE(registry.isSystemOnly("MediaJob", "read"));
    EXPECT_FALSE(registry.isSystemOnly("MediaJob", "delete"));
}

// The array form: an operation may be a list of alternatives, any one of
// which grants access (youtube_clone/video.json uses this shape).
TEST(SchemaAclRegistry, ArrayFormAclOperationIsParsed) {
    TempSchemaDir dir;
    dir.writeSchema("Video.json", R"({
        "entity": "Video",
        "fields": { "id": { "type": "string" } },
        "acl": {
            "read":   [ { "authenticated": true, "published": true },
                        { "uploaderId": "{{ currentUserId }}" } ],
            "create": [ { "system": true } ]
        }
    })");

    SchemaAclRegistry registry(dir.path());
    EXPECT_TRUE(registry.isSystemOnly("Video", "create"));
    EXPECT_FALSE(registry.isSystemOnly("Video", "read"));
}

// A schema that indexes tenantId without declaring it as a field relies on
// the top-level "tenantId": true auto-add. Without it the index failed
// validation, the entity was dropped, and its ACL went unenforced.
TEST(SchemaAclRegistry, ImplicitTenantIdFieldDoesNotDropTheSchema) {
    TempSchemaDir dir;
    dir.writeSchema("Notification.json", R"({
        "entity": "Notification",
        "tenantId": true,
        "fields": { "id": { "type": "string" } },
        "indexes": [ { "fields": ["tenantId"] } ],
        "acl": { "create": { "system": true } }
    })");

    SchemaAclRegistry registry(dir.path());
    EXPECT_TRUE(registry.isSystemOnly("Notification", "create"));
}


// ── role ACLs ───────────────────────────────────────────────────────────────

TEST(SchemaAclRegistry, RolesAreReadFromAList) {
    TempSchemaDir dir;
    dir.writeSchema("StyleRule.json", R"({
        "entity": "StyleRule",
        "fields": { "id": { "type": "string", "primary": true } },
        "acl": {
            "create": { "role": ["god", "supergod"] },
            "read":   { "public": true },
            "delete": { "role": ["god", "supergod"] }
        }
    })");
    SchemaAclRegistry registry(dir.path());

    EXPECT_EQ(registry.requiredRoles("StyleRule", "create"),
              (std::vector<std::string>{"god", "supergod"}));
    EXPECT_TRUE(registry.roleAllowed("StyleRule", "create", "god"));
    EXPECT_FALSE(registry.roleAllowed("StyleRule", "create", "user"));
    // No token means no role, which must not pass a restricted operation.
    EXPECT_FALSE(registry.roleAllowed("StyleRule", "create", ""));
}

TEST(SchemaAclRegistry, RolesAreReadFromASingleString) {
    TempSchemaDir dir;
    dir.writeSchema("Note.json", R"({
        "entity": "Note",
        "fields": { "id": { "type": "string", "primary": true } },
        "acl": { "update": { "role": "user" } }
    })");
    SchemaAclRegistry registry(dir.path());
    EXPECT_TRUE(registry.roleAllowed("Note", "update", "user"));
    EXPECT_FALSE(registry.roleAllowed("Note", "update", "guest"));
}

TEST(SchemaAclRegistry, OperationWithNoRolesIsUnrestricted) {
    TempSchemaDir dir;
    dir.writeSchema("Note.json", R"({
        "entity": "Note",
        "fields": { "id": { "type": "string", "primary": true } },
        "acl": { "read": { "public": true } }
    })");
    SchemaAclRegistry registry(dir.path());
    // Fails open, exactly as isSystemOnly does: an entity nobody wrote a rule
    // for keeps working rather than becoming unreachable.
    EXPECT_TRUE(registry.requiredRoles("Note", "create").empty());
    EXPECT_TRUE(registry.roleAllowed("Note", "create", ""));
}

TEST(SchemaAclRegistry, UnknownEntityIsUnrestricted) {
    TempSchemaDir dir;
    SchemaAclRegistry registry(dir.path());
    EXPECT_TRUE(registry.roleAllowed("NoSuchEntity", "create", ""));
}

TEST(SchemaAclRegistry, RolesDoNotDisturbBooleanPredicates) {
    TempSchemaDir dir;
    dir.writeSchema("Mixed.json", R"({
        "entity": "Mixed",
        "fields": { "id": { "type": "string", "primary": true } },
        "acl": { "delete": { "role": ["god"], "system": true } }
    })");
    SchemaAclRegistry registry(dir.path());
    // One unmodelled value used to throw and discard the whole schema file,
    // silently unenforcing every system-only entity in it.
    EXPECT_TRUE(registry.isSystemOnly("Mixed", "delete"));
    EXPECT_EQ(registry.requiredRoles("Mixed", "delete"),
              (std::vector<std::string>{"god"}));
}


// -- default-deny writes, and privileged fields ----------------------------

TEST(SchemaAclRegistry, PublicWriteIsRecognised) {
    TempSchemaDir dir;
    dir.writeSchema("User.json", R"({
        "entity": "User",
        "fields": {
            "id":   { "type": "string", "primary": true },
            "role": { "type": "string", "privileged": true }
        },
        "acl": { "create": { "public": true }, "update": { "self": true } }
    })");
    SchemaAclRegistry registry(dir.path());

    // Signing up must work without already being signed in.
    EXPECT_TRUE(registry.isPublicWrite("User", "create"));
    // "self" is a predicate nothing enforces; it must not read as public, or
    // anyone could update any user -- role included.
    EXPECT_FALSE(registry.isPublicWrite("User", "update"));
    EXPECT_FALSE(registry.isPublicWrite("User", "delete"));
}

TEST(SchemaAclRegistry, PrivilegedFieldsAreListed) {
    TempSchemaDir dir;
    dir.writeSchema("User.json", R"({
        "entity": "User",
        "fields": {
            "id":       { "type": "string", "primary": true },
            "username": { "type": "string" },
            "role":     { "type": "string", "privileged": true }
        },
        "acl": { "create": { "public": true } }
    })");
    SchemaAclRegistry registry(dir.path());
    EXPECT_EQ(registry.privilegedFields("User"),
              (std::vector<std::string>{"role"}));
}

TEST(SchemaAclRegistry, NoPrivilegedFieldsByDefault) {
    TempSchemaDir dir;
    dir.writeSchema("Note.json", R"({
        "entity": "Note",
        "fields": { "id": { "type": "string", "primary": true } },
        "acl": { "create": { "public": true } }
    })");
    SchemaAclRegistry registry(dir.path());
    EXPECT_TRUE(registry.privilegedFields("Note").empty());
}

TEST(SchemaAclRegistry, EntityWithNoWriteRuleIsNotPublic) {
    TempSchemaDir dir;
    dir.writeSchema("Snippet.json", R"({
        "entity": "Snippet",
        "fields": { "id": { "type": "string", "primary": true } },
        "acl": { "read": { "public": true } }
    })");
    SchemaAclRegistry registry(dir.path());
    // Twenty-five shipped entities look like this. A readable entity is not a
    // writable one, and the write path must default to requiring a caller.
    EXPECT_FALSE(registry.isPublicWrite("Snippet", "create"));
    EXPECT_TRUE(registry.isPublicWrite("Snippet", "read"));
}
