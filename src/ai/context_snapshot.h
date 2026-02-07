#ifndef slic3r_ai_context_snapshot_h_
#define slic3r_ai_context_snapshot_h_

#include <string>

namespace Slic3r {
namespace GUI {
class Plater;
}

namespace AI {

// Builds the read-only AI context snapshot JSON for the current plater state.
std::string build_context_snapshot_json(GUI::Plater& plater);

} // namespace AI
} // namespace Slic3r

#endif // slic3r_ai_context_snapshot_h_
