#ifndef slic3r_ai_providers_fake_provider_h_
#define slic3r_ai_providers_fake_provider_h_

#include "provider.h"

namespace Slic3r {
namespace AI {
namespace Providers {

class FakeProvider final : public Provider
{
public:
    std::string run(const ProviderRequest& request) const override;
};

} // namespace Providers
} // namespace AI
} // namespace Slic3r

#endif // slic3r_ai_providers_fake_provider_h_
