#include "fake_provider.h"

#include <algorithm>
#include <cctype>
#include <string>

#include "nlohmann/json.hpp"

namespace Slic3r {
namespace AI {
namespace Providers {

namespace {

using nlohmann::json;

std::string to_lower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

} // namespace

std::string FakeProvider::run(const ProviderRequest& request) const
{
    const bool has_context  = !request.context_snapshot_json.empty();
    const bool has_geometry = !request.geometry_insights_json.empty();
    const std::string message_lc = to_lower(request.user_message);

    const bool prefers_quality = message_lc.find("quality") != std::string::npos;
    const bool prefers_speed   = message_lc.find("speed") != std::string::npos || message_lc.find("faster") != std::string::npos;

    json response = json::object();
    response["contract_version"] = "0.1.0";

    if (prefers_quality) {
        response["summary"] = "Prioriser la qualite avec une couche plus fine et un remplissage plus regulier.";
    } else if (prefers_speed) {
        response["summary"] = "Prioriser le temps d'impression avec une couche plus haute et un remplissage simplifie.";
    } else {
        response["summary"] = "Ajustements slicer proposes de maniere prudente pour un compromis qualite/temps.";
    }

    json changes = json::array();
    if (prefers_speed) {
        changes.push_back(json{
            {"id", "chg-layer-height-speed"},
            {"key", "layer_height"},
            {"value", 0.28},
            {"value_type", "float"},
            {"reason", "Augmenter la hauteur de couche reduit le nombre de couches et accelere l'impression."},
            {"impact", json{{"quality", 5}, {"time", 8}, {"risk", 4}}},
            {"confidence", has_context ? 0.85 : 0.62},
            {"applies_to", "profile"},
            {"tags", json::array({"speed", "layering"})},
            {"requires_user_confirmation", true}
        });
    } else {
        changes.push_back(json{
            {"id", "chg-layer-height-quality"},
            {"key", "layer_height"},
            {"value", 0.16},
            {"value_type", "float"},
            {"reason", "Reduire la hauteur de couche ameliore le rendu de surface."},
            {"impact", json{{"quality", 8}, {"time", 4}, {"risk", 3}}},
            {"confidence", has_context ? 0.87 : 0.64},
            {"applies_to", "profile"},
            {"tags", json::array({"quality", "surface"})},
            {"requires_user_confirmation", true}
        });
    }

    changes.push_back(json{
        {"id", "chg-detect-thin-walls"},
        {"key", "detect_thin_wall"},
        {"value", true},
        {"value_type", "bool"},
        {"reason", "Aider a conserver les details fins selon la geometrie du modele."},
        {"impact", json{{"quality", 7}, {"time", 6}, {"risk", 3}}},
        {"confidence", has_geometry ? 0.86 : 0.55},
        {"applies_to", "global"},
        {"tags", json::array({"geometry", "detail"})},
        {"requires_user_confirmation", true}
    });

    response["recommended_changes"] = std::move(changes);
    response["questions"] = json::array();
    response["questions"].push_back("Souhaitez-vous prioriser la qualite visuelle ou le temps d'impression ?");
    response["refusals"] = json::array();

    json warnings = json::array();
    if (!has_context)
        warnings.push_back("context_snapshot_missing");
    if (!has_geometry)
        warnings.push_back("geometry_insights_missing");
    response["warnings"] = std::move(warnings);

    return response.dump(2);
}

} // namespace Providers
} // namespace AI
} // namespace Slic3r
