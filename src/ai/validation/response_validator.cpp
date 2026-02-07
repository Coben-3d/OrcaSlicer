#include "response_validator.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

namespace Slic3r {
namespace AI {
namespace Validation {

namespace {

using nlohmann::json;

std::filesystem::path default_schema_path()
{
    return std::filesystem::path(__FILE__).parent_path().parent_path() / "contracts" / "response_schema_v0_1_0.json";
}

bool load_json_file(const std::string& path, json& out, std::string& error)
{
    std::ifstream file(path, std::ios::in | std::ios::binary);
    if (!file.is_open()) {
        error = "Cannot open schema file: " + path;
        return false;
    }

    try {
        file >> out;
        return true;
    } catch (const std::exception& ex) {
        error = std::string("Failed to parse schema JSON: ") + ex.what();
        return false;
    }
}

bool matches_type_name(const json& value, const std::string& type_name)
{
    if (type_name == "object")  return value.is_object();
    if (type_name == "array")   return value.is_array();
    if (type_name == "string")  return value.is_string();
    if (type_name == "boolean") return value.is_boolean();
    if (type_name == "number")  return value.is_number();
    if (type_name == "integer") return value.is_number_integer();
    if (type_name == "null")    return value.is_null();
    return false;
}

bool matches_type(const json& value, const json& schema_type)
{
    if (schema_type.is_string())
        return matches_type_name(value, schema_type.get<std::string>());
    if (schema_type.is_array()) {
        for (const auto& type_name : schema_type) {
            if (type_name.is_string() && matches_type_name(value, type_name.get<std::string>()))
                return true;
        }
    }
    return false;
}

void validate_against_schema(const json& value, const json& schema, const std::string& path, std::vector<std::string>& errors)
{
    if (schema.contains("const") && value != schema.at("const"))
        errors.push_back(path + ": value does not match const.");

    if (schema.contains("type") && !matches_type(value, schema.at("type"))) {
        errors.push_back(path + ": type mismatch.");
        return;
    }

    if (schema.contains("enum") && schema.at("enum").is_array()) {
        bool found = false;
        for (const auto& allowed : schema.at("enum")) {
            if (value == allowed) {
                found = true;
                break;
            }
        }
        if (!found)
            errors.push_back(path + ": value is not in enum.");
    }

    if (value.is_number()) {
        if (schema.contains("minimum") && value.get<double>() < schema.at("minimum").get<double>())
            errors.push_back(path + ": value is below minimum.");
        if (schema.contains("maximum") && value.get<double>() > schema.at("maximum").get<double>())
            errors.push_back(path + ": value is above maximum.");
    }

    if (value.is_array()) {
        if (schema.contains("minItems") && value.size() < schema.at("minItems").get<size_t>())
            errors.push_back(path + ": array has too few items.");
        if (schema.contains("maxItems") && value.size() > schema.at("maxItems").get<size_t>())
            errors.push_back(path + ": array has too many items.");
        if (schema.contains("items") && schema.at("items").is_object()) {
            for (size_t i = 0; i < value.size(); ++i)
                validate_against_schema(value[i], schema.at("items"), path + "[" + std::to_string(i) + "]", errors);
        }
        return;
    }

    if (!value.is_object())
        return;

    if (schema.contains("required") && schema.at("required").is_array()) {
        for (const auto& required_key : schema.at("required")) {
            if (!required_key.is_string())
                continue;
            const std::string key = required_key.get<std::string>();
            if (!value.contains(key))
                errors.push_back(path + ": missing required field '" + key + "'.");
        }
    }

    const bool additional_allowed = !schema.contains("additionalProperties") || schema.at("additionalProperties").get<bool>();
    if (!additional_allowed && schema.contains("properties") && schema.at("properties").is_object()) {
        for (auto it = value.begin(); it != value.end(); ++it) {
            if (!schema.at("properties").contains(it.key()))
                errors.push_back(path + ": unexpected field '" + it.key() + "'.");
        }
    }

    if (schema.contains("properties") && schema.at("properties").is_object()) {
        for (auto prop_it = schema.at("properties").begin(); prop_it != schema.at("properties").end(); ++prop_it) {
            if (!value.contains(prop_it.key()))
                continue;
            if (!prop_it.value().is_object())
                continue;
            validate_against_schema(value.at(prop_it.key()), prop_it.value(), path + "." + prop_it.key(), errors);
        }
    }
}

void validate_business_rules(const json& response, std::vector<std::string>& errors)
{
    if (!response.contains("contract_version") || !response.at("contract_version").is_string() ||
        response.at("contract_version").get<std::string>() != "0.1.0") {
        errors.push_back("business: contract_version must be '0.1.0'.");
    }

    if (!response.contains("recommended_changes") || !response.at("recommended_changes").is_array()) {
        errors.push_back("business: recommended_changes must be an array.");
        return;
    }

    const auto& changes = response.at("recommended_changes");
    if (changes.size() > 20)
        errors.push_back("business: recommended_changes must be <= 20.");

    for (size_t i = 0; i < changes.size(); ++i) {
        const auto& change = changes[i];
        if (!change.is_object())
            continue;

        if (change.contains("impact") && change.at("impact").is_object()) {
            const auto& impact = change.at("impact");
            for (const char* key : {"quality", "time", "risk"}) {
                if (!impact.contains(key) || !impact.at(key).is_number_integer()) {
                    errors.push_back("business: recommended_changes[" + std::to_string(i) + "].impact." + key + " must be integer.");
                    continue;
                }
                const int value = impact.at(key).get<int>();
                if (value < 1 || value > 10) {
                    errors.push_back("business: recommended_changes[" + std::to_string(i) + "].impact." + key + " must be in [1,10].");
                }
            }
        } else {
            errors.push_back("business: recommended_changes[" + std::to_string(i) + "].impact must be object.");
        }

        if (!change.contains("confidence") || !change.at("confidence").is_number()) {
            errors.push_back("business: recommended_changes[" + std::to_string(i) + "].confidence must be number.");
            continue;
        }
        const double confidence = change.at("confidence").get<double>();
        if (confidence < 0.0 || confidence > 1.0)
            errors.push_back("business: recommended_changes[" + std::to_string(i) + "].confidence must be in [0,1].");
    }
}

} // namespace

ResponseValidator::ResponseValidator()
{
    m_schema_path = default_schema_path().string();

    json schema_json;
    std::string schema_error;
    if (!load_json_file(m_schema_path, schema_json, schema_error)) {
        m_schema_loaded = false;
        m_schema_text = schema_error;
        return;
    }

    m_schema_loaded = true;
    m_schema_text = schema_json.dump();
}

ValidationResult ResponseValidator::validate(const std::string& raw_response_json) const
{
    ValidationResult result;

    json response;
    try {
        response = json::parse(raw_response_json);
    } catch (const std::exception& ex) {
        result.errors.push_back(std::string("parse: invalid JSON: ") + ex.what());
        return result;
    }

    if (!m_schema_loaded) {
        result.errors.push_back("schema: " + m_schema_text);
        return result;
    }

    json schema;
    try {
        schema = json::parse(m_schema_text);
    } catch (const std::exception& ex) {
        result.errors.push_back(std::string("schema: cannot parse loaded schema: ") + ex.what());
        return result;
    }

    validate_against_schema(response, schema, "$", result.errors);
    validate_business_rules(response, result.errors);
    result.valid = result.errors.empty();
    return result;
}

} // namespace Validation
} // namespace AI
} // namespace Slic3r
