#ifndef slic3r_ai_apply_allowlist_registry_h_
#define slic3r_ai_apply_allowlist_registry_h_

#include <cstddef>
#include <initializer_list>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

namespace Slic3r {
namespace AI {
namespace Apply {

enum class AllowlistValueType
{
    Bool,
    Int,
    Float,
    String,
    Enum
};

struct AllowlistEntry
{
    std::string               key;
    AllowlistValueType        type { AllowlistValueType::String };
    std::string               label;
    std::string               safety_notes;
    std::vector<std::string>  tags;
    bool                      has_numeric_bounds { false };
    double                    min_value { 0.0 };
    double                    max_value { 0.0 };
    bool                      has_length_bounds { false };
    size_t                    min_length { 0 };
    size_t                    max_length { 0 };
    std::vector<std::string>  enum_values;
};

struct ValidationResult
{
    bool        valid { false };
    std::string error_message;
};

class AllowlistRegistry
{
public:
    static bool        is_allowed(const std::string& key);
    static ValidationResult validate_value(const std::string& key, const nlohmann::json& json_value);
    static std::string label_for(const std::string& key);
    static std::string safety_notes_for(const std::string& key);
    static bool        has_tag(const std::string& key, const std::string& tag);
    static bool        has_any_tag(const std::string& key, std::initializer_list<std::string> tags);
    static size_t      key_count();
};

} // namespace Apply
} // namespace AI
} // namespace Slic3r

#endif // slic3r_ai_apply_allowlist_registry_h_
