#pragma once

#include <string>
#include <vector>
#include <map>
#include <nlohmann/json.hpp>
#include "dbal/core/entity_loader.hpp"

namespace dbal {
namespace core {

struct EntityIndex;

namespace loaders {

class RelationParser {
public:
    EntityIndex parseIndex(const nlohmann::json& indexNode);
    EntitySchema::ACL parseACL(const nlohmann::json& aclNode);

private:
    std::vector<std::string> parseIndexFields(const nlohmann::json& indexNode);
    std::map<std::string, bool> parseACLOperation(const nlohmann::json& operationNode);
    /// Roles named by one acl.<op> block. Accepts a single role or a list.
    static std::vector<std::string> parseACLRoles(const nlohmann::json& operationNode);
};

}  // namespace loaders
}  // namespace core
}  // namespace dbal
