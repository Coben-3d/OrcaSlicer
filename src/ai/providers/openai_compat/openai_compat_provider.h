#ifndef slic3r_ai_providers_openai_compat_provider_h_
#define slic3r_ai_providers_openai_compat_provider_h_

#include <string>

#include "../provider.h"

namespace Slic3r {
namespace AI {
namespace Providers {

struct OpenAICompatConfig
{
    std::string provider_type;
    std::string base_url;
    std::string api_key;
    std::string model;
    long        timeout_seconds { 30 };
    int         max_tokens { 600 };
    double      temperature { 0.2 };
    bool        use_json_schema_response_format { true };
};

class OpenAICompatProvider final : public Provider
{
public:
    explicit OpenAICompatProvider(OpenAICompatConfig config);

    std::string run(const ProviderRequest& request) const override;
    bool test_connection(std::string& error_message) const;

    static bool save_api_key_securely(const std::string& api_key, std::string& error_message);
    static bool load_api_key_securely(std::string& api_key, std::string& error_message);
    static bool clear_api_key_securely(std::string& error_message);

private:
    OpenAICompatConfig m_config;
};

} // namespace Providers
} // namespace AI
} // namespace Slic3r

#endif // slic3r_ai_providers_openai_compat_provider_h_
