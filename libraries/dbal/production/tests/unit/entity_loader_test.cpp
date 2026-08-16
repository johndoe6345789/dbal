/**
 * @file entity_loader_test.cpp
 * @brief Unit tests for EntitySchemaLoader
 */

#include "dbal/core/entity_loader.hpp"
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

using namespace dbal::core;

namespace {

/**
 * Resolves a path under shared/api/schema relative to wherever the binary is
 * run from.
 *
 * These tests used to hardcode "../dbal/shared/api/schema/entities/core/
 * user.yaml". Two things had rotted: the schemas migrated from YAML to JSON
 * (yaml-cpp was dropped entirely), and the single "../dbal/..." prefix only
 * resolved from one long-gone working directory. Since the file was in no
 * CMake target, nothing ever ran it and neither break was noticed.
 *
 * DBAL_SCHEMA_ROOT overrides; otherwise walk up looking for the tree.
 */
std::string schemaPath(const std::string& relative) {
    if (const char* root = std::getenv("DBAL_SCHEMA_ROOT")) {
        return (std::filesystem::path(root) / relative).string();
    }
    auto dir = std::filesystem::current_path();
    for (int depth = 0; depth < 6; ++depth) {
        auto candidate = dir / "shared" / "api" / "schema" / relative;
        if (std::filesystem::exists(candidate)) return candidate.string();
        auto nested = dir / "libraries" / "dbal" / "shared" / "api" / "schema" / relative;
        if (std::filesystem::exists(nested)) return nested.string();
        if (!dir.has_parent_path() || dir.parent_path() == dir) break;
        dir = dir.parent_path();
    }
    return {};  // caller skips
}

bool schemasAvailable() {
    return !schemaPath("entities/core/user.json").empty();
}

} // namespace

void testLoadSingleSchema() {
    std::cout << "Testing single schema load..." << std::endl;

    EntitySchemaLoader loader;

    // Test loading user.yaml schema
    try {
        auto schema = loader.loadSchema(schemaPath("entities/core/user.json"));

        // Verify basic metadata
        assert(schema.name == "User");
        assert(schema.version == "1.0");
        assert(!schema.description.empty());

        // Verify fields exist
        assert(!schema.fields.empty());

        // Find and verify specific fields
        bool foundId = false;
        bool foundEmail = false;
        bool foundRole = false;

        for (const auto& field : schema.fields) {
            if (field.name == "id") {
                foundId = true;
                // "string", not "uuid": User.id is a plain string primary key,
                // matching the generic SQL adapter's hardcoded WHERE id = ?.
                // The old "uuid" expectation predates that and was never
                // caught because this file was in no CMake target.
                assert(field.type == "string");
                assert(field.primary);
                assert(field.generated);
            } else if (field.name == "email") {
                foundEmail = true;
                assert(field.type == "email");
                assert(field.required);
                assert(field.unique);
            } else if (field.name == "role") {
                foundRole = true;
                assert(field.type == "enum");
                assert(field.required);
                assert(field.enumValues.has_value());
                assert(field.enumValues->size() == 6);  // public, user, moderator, admin, god, supergod
            }
        }

        assert(foundId);
        assert(foundEmail);
        assert(foundRole);

        // Verify indexes exist
        assert(!schema.indexes.empty());

        // Verify ACL exists
        assert(schema.acl.has_value());

        std::cout << "✓ Single schema load test passed" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "✗ Single schema load failed: " << e.what() << std::endl;
        throw;
    }
}

void testLoadAllSchemas() {
    std::cout << "Testing all schemas load..." << std::endl;

    EntitySchemaLoader loader;

    try {
        auto schemas = loader.loadSchemas(schemaPath("entities"));

        // Verify we loaded multiple schemas
        assert(schemas.size() > 0);

        std::cout << "Loaded " << schemas.size() << " schemas:" << std::endl;
        for (const auto& [name, schema] : schemas) {
            std::cout << "  - " << name << " (" << schema.displayName << ")" << std::endl;
        }

        // Verify core entities are present
        assert(schemas.find("User") != schemas.end());
        assert(schemas.find("Session") != schemas.end());
        assert(schemas.find("Workflow") != schemas.end());

        std::cout << "✓ All schemas load test passed" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "✗ All schemas load failed: " << e.what() << std::endl;
        throw;
    }
}

void testFieldParsing() {
    std::cout << "Testing field parsing..." << std::endl;

    EntitySchemaLoader loader;

    try {
        auto schema = loader.loadSchema(schemaPath("entities/packages/notification.json"));

        // Verify entity name
        assert(schema.name == "Notification");

        // Find fields with specific properties
        bool foundEnum = false;
        bool foundJson = false;
        bool foundIndexed = false;

        for (const auto& field : schema.fields) {
            if (field.name == "type") {
                foundEnum = true;
                assert(field.type == "enum");
                assert(field.enumValues.has_value());
                assert(field.enumValues->size() == 9);  // info, warning, success, error, mention, reply, follow, like, system
            } else if (field.name == "data") {
                foundJson = true;
                assert(field.type == "json");
                assert(field.nullable);
            } else if (field.name == "userId") {
                foundIndexed = true;
                assert(field.index);
                assert(field.required);
            }
        }

        assert(foundEnum);
        assert(foundJson);
        assert(foundIndexed);

        // Verify multi-field indexes
        assert(!schema.indexes.empty());

        // Find user_unread index
        bool foundUserUnreadIndex = false;
        for (const auto& index : schema.indexes) {
            if (index.name.has_value() && index.name.value() == "user_unread") {
                foundUserUnreadIndex = true;
                assert(index.fields.size() == 2);
                assert(index.fields[0] == "userId");
                assert(index.fields[1] == "read");
            }
        }

        assert(foundUserUnreadIndex);

        std::cout << "✓ Field parsing test passed" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "✗ Field parsing test failed: " << e.what() << std::endl;
        throw;
    }
}

void testACLParsing() {
    std::cout << "Testing ACL parsing..." << std::endl;

    EntitySchemaLoader loader;

    try {
        auto schema = loader.loadSchema(schemaPath("entities/core/user.json"));

        assert(schema.acl.has_value());

        const auto& acl = schema.acl.value();

        // Verify create permissions
        assert(acl.create.find("public") != acl.create.end());
        assert(acl.create.at("public") == true);

        // Verify read permissions
        assert(acl.read.find("self") != acl.read.end());
        assert(acl.read.at("self") == true);
        assert(acl.read.find("admin") != acl.read.end());
        assert(acl.read.at("admin") == true);

        // Verify update permissions
        assert(acl.update.find("self") != acl.update.end());
        assert(acl.update.at("self") == true);

        // Verify delete permissions
        assert(acl.del.find("admin") != acl.del.end());
        assert(acl.del.at("admin") == true);

        std::cout << "✓ ACL parsing test passed" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "✗ ACL parsing test failed: " << e.what() << std::endl;
        throw;
    }
}

int main() {
    std::cout << "=== EntitySchemaLoader Unit Tests ===" << std::endl;

    // These read the real schema tree rather than fixtures, so they can only
    // run where it is reachable. Skip cleanly instead of failing, matching
    // EntitySchemaLoaderTest.LoadSchemaIntegration in the gtest suite.
    if (!schemasAvailable()) {
        std::cout << "SKIP: schema tree not found from " << std::filesystem::current_path()
                  << " (set DBAL_SCHEMA_ROOT to shared/api/schema to run these)" << std::endl;
        return 0;
    }

    try {
        testLoadSingleSchema();
        testLoadAllSchemas();
        testFieldParsing();
        testACLParsing();

        std::cout << "\n✓ All tests passed!" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\n✗ Tests failed: " << e.what() << std::endl;
        return 1;
    }
}
