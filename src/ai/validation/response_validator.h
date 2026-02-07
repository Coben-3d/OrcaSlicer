#ifndef slic3r_ai_validation_response_validator_h_
#define slic3r_ai_validation_response_validator_h_

#include <string>
#include <vector>

namespace Slic3r {
namespace AI {
namespace Validation {

struct ValidationResult
{
    bool valid { false };
    std::vector<std::string> errors;
};

class ResponseValidator
{
public:
    ResponseValidator();
    ValidationResult validate(const std::string& raw_response_json) const;

private:
    std::string m_schema_path;
    std::string m_schema_text;
    bool        m_schema_loaded { false };
};

} // namespace Validation
} // namespace AI
} // namespace Slic3r

#endif // slic3r_ai_validation_response_validator_h_
