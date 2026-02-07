#ifndef slic3r_ai_providers_provider_h_
#define slic3r_ai_providers_provider_h_

#include <string>

namespace Slic3r {
namespace AI {
namespace Providers {

struct ProviderRequest
{
    std::string user_message;
    std::string context_snapshot_json;
    std::string geometry_insights_json;
};

class Provider
{
public:
    virtual ~Provider() = default;
    virtual std::string run(const ProviderRequest& request) const = 0;
};

} // namespace Providers
} // namespace AI
} // namespace Slic3r

#endif // slic3r_ai_providers_provider_h_
