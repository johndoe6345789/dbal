#include "adapters/json_column.hpp"

namespace dbal::adapters {

nlohmann::json decodeJsonColumn(const std::string& value) {
    try {
        return nlohmann::json::parse(value);
    } catch (const nlohmann::json::parse_error&) {
        return value;
    }
}

} // namespace dbal::adapters
