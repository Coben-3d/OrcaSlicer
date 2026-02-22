#include "allowlist_registry.h"

#include <algorithm>
#include <array>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Slic3r {
namespace AI {
namespace Apply {

namespace {

std::vector<std::string> derive_tags_for_key(const std::string& key)
{
    std::vector<std::string> tags;
    auto add_tag = [&tags](const std::string& tag) {
        if (std::find(tags.begin(), tags.end(), tag) == tags.end())
            tags.push_back(tag);
    };

    if (key.find("temperature") != std::string::npos) {
        add_tag("temperature");
        add_tag("high-risk");
    }

    if (key.find("speed") != std::string::npos)
        add_tag("speed");

    if (key.find("flow") != std::string::npos)
        add_tag("flow");

    static const std::array<const char*, 5> high_risk_flow_keys = {
        "flow_ratio",
        "pressure_advance",
        "volumetric_flow_limit",
        "max_volumetric_speed",
        "bridge_flow_ratio"
    };
    if (std::any_of(high_risk_flow_keys.begin(), high_risk_flow_keys.end(), [&key](const char* item) { return key == item; })) {
        add_tag("flow");
        add_tag("high-risk");
    }

    return tags;
}

AllowlistEntry bool_entry(std::string key, std::string label, std::string notes)
{
    AllowlistEntry entry;
    entry.key          = std::move(key);
    entry.type         = AllowlistValueType::Bool;
    entry.label        = std::move(label);
    entry.safety_notes = std::move(notes);
    entry.tags         = derive_tags_for_key(entry.key);
    entry.enum_values  = {"false", "true"};
    return entry;
}

AllowlistEntry int_entry(std::string key, int min_value, int max_value, std::string label, std::string notes)
{
    AllowlistEntry entry;
    entry.key                = std::move(key);
    entry.type               = AllowlistValueType::Int;
    entry.label              = std::move(label);
    entry.safety_notes       = std::move(notes);
    entry.tags               = derive_tags_for_key(entry.key);
    entry.has_numeric_bounds = true;
    entry.min_value          = static_cast<double>(min_value);
    entry.max_value          = static_cast<double>(max_value);
    return entry;
}

AllowlistEntry float_entry(std::string key, double min_value, double max_value, std::string label, std::string notes)
{
    AllowlistEntry entry;
    entry.key                = std::move(key);
    entry.type               = AllowlistValueType::Float;
    entry.label              = std::move(label);
    entry.safety_notes       = std::move(notes);
    entry.tags               = derive_tags_for_key(entry.key);
    entry.has_numeric_bounds = true;
    entry.min_value          = min_value;
    entry.max_value          = max_value;
    return entry;
}

AllowlistEntry string_entry(std::string key, size_t min_length, size_t max_length, std::string label, std::string notes)
{
    AllowlistEntry entry;
    entry.key               = std::move(key);
    entry.type              = AllowlistValueType::String;
    entry.label             = std::move(label);
    entry.safety_notes      = std::move(notes);
    entry.tags              = derive_tags_for_key(entry.key);
    entry.has_length_bounds = true;
    entry.min_length        = min_length;
    entry.max_length        = max_length;
    return entry;
}

AllowlistEntry enum_entry(std::string key, std::vector<std::string> enum_values, std::string label, std::string notes)
{
    AllowlistEntry entry;
    entry.key          = std::move(key);
    entry.type         = AllowlistValueType::Enum;
    entry.label        = std::move(label);
    entry.safety_notes = std::move(notes);
    entry.tags         = derive_tags_for_key(entry.key);
    entry.enum_values  = std::move(enum_values);
    return entry;
}

const std::unordered_map<std::string, AllowlistEntry>& registry()
{
    static const std::unordered_map<std::string, AllowlistEntry> entries = [] {
        const std::vector<AllowlistEntry> list = {
            float_entry("layer_height", 0.04, 0.40, "Layer Height", "Lower values improve quality but may increase print time sharply."),
            float_entry("first_layer_height", 0.04, 0.50, "First Layer Height", "Too high may reduce bed adhesion consistency."),
            float_entry("line_width", 0.20, 1.20, "Line Width", "Extreme widths can break extrusion assumptions."),
            float_entry("first_layer_line_width", 0.20, 1.50, "First Layer Line Width", "High values may cause elephant foot and over-extrusion."),
            int_entry("wall_loops", 1, 12, "Wall Loops", "More walls improve strength but can trap heat and increase time."),
            int_entry("top_shell_layers", 0, 20, "Top Shell Layers", "Too few layers can expose infill on top surfaces."),
            int_entry("bottom_shell_layers", 0, 20, "Bottom Shell Layers", "Too few layers can weaken base integrity."),
            float_entry("top_shell_thickness", 0.0, 5.0, "Top Shell Thickness", "Large values increase time and material use."),
            float_entry("bottom_shell_thickness", 0.0, 5.0, "Bottom Shell Thickness", "Large values increase weight and duration."),
            enum_entry("seam_position", {"nearest", "aligned", "random", "rear"}, "Seam Position", "Seam placement affects surface aesthetics."),
            bool_entry("external_perimeters_first", "External Perimeters First", "May improve finish but can reduce support from inner walls."),
            bool_entry("detect_thin_wall", "Detect Thin Wall", "May introduce narrow toolpaths that are harder to print reliably."),
            bool_entry("ensure_vertical_shell_thickness", "Ensure Vertical Shell Thickness", "Can change local wall strategy and print time."),
            enum_entry("ironing_type", {"no_ironing", "top_surfaces", "all_top_surfaces"}, "Ironing Type", "Ironing can overheat top surfaces on some materials."),
            float_entry("ironing_spacing", 0.05, 1.00, "Ironing Spacing", "Very tight spacing can overcook top layers."),
            float_entry("ironing_flow", 1.0, 100.0, "Ironing Flow", "Too much flow causes blobs and gloss artifacts."),
            float_entry("ironing_speed", 1.0, 300.0, "Ironing Speed", "Very slow ironing can increase heat accumulation."),
            enum_entry("fuzzy_skin", {"none", "external", "all"}, "Fuzzy Skin", "Useful for texture but may break dimensional accuracy."),
            float_entry("fuzzy_skin_thickness", 0.0, 0.5, "Fuzzy Skin Thickness", "Large offsets can cause self-overlap."),
            float_entry("fuzzy_skin_point_dist", 0.05, 1.0, "Fuzzy Skin Point Distance", "Very low distance can bloat path complexity."),
            bool_entry("arc_fitting", "Arc Fitting", "May reduce file size but can alter motion on unsupported firmware."),
            float_entry("resolution", 0.005, 0.2, "Resolution", "Very low values increase toolpath complexity and file size."),
            float_entry("sparse_infill_density", 0.0, 100.0, "Sparse Infill Density", "High density can dramatically increase material/time."),
            enum_entry("sparse_infill_pattern", {"grid", "gyroid", "cubic", "line", "triangles"}, "Sparse Infill Pattern", "Pattern changes affect strength and resonance."),
            enum_entry("internal_solid_infill_pattern", {"rectilinear", "grid", "concentric"}, "Internal Solid Infill Pattern", "Pattern choice affects top-layer support quality."),
            bool_entry("infill_combination", "Infill Combination", "Can reduce path count but alter local infill response."),
            bool_entry("infill_anchor", "Infill Anchor", "Anchors may improve bonding at cost of extra travel."),
            float_entry("infill_anchor_max", 0.0, 100.0, "Infill Anchor Max", "Very large anchors can over-constrain path planning."),
            int_entry("solid_infill_every_layers", 0, 20, "Solid Infill Every N Layers", "High frequency increases rigidity and print time."),
            int_entry("fill_angle", 0, 359, "Fill Angle", "Angle changes can impact anisotropic strength."),
            int_entry("bridge_angle", 0, 359, "Bridge Angle", "Wrong angle can worsen unsupported spans."),
            bool_entry("detect_overhang_wall", "Detect Overhang Wall", "May alter perimeters for steep regions."),
            float_entry("bridge_flow_ratio", 0.5, 2.0, "Bridge Flow Ratio", "Excessive flow can sag and fuse strands."),
            float_entry("bridge_speed", 5.0, 300.0, "Bridge Speed", "Bridge speed has direct impact on sagging risk."),
            float_entry("bridge_internal_speed", 5.0, 300.0, "Bridge Internal Speed", "Internal bridge speed can alter support for top layers."),
            bool_entry("support_enable", "Enable Support", "Global support changes can massively affect material use."),
            enum_entry("support_style", {"normal", "snug", "tree"}, "Support Style", "Tree supports may conflict with tight clearances."),
            enum_entry("support_type", {"normal", "tree"}, "Support Type", "Type influences contact and removal behavior."),
            int_entry("support_threshold_angle", 0, 90, "Support Threshold Angle", "Lower thresholds generate more supports."),
            bool_entry("support_on_build_plate_only", "Support On Build Plate Only", "May leave upper overhangs unsupported."),
            bool_entry("support_critical_regions_only", "Support Critical Regions Only", "Can miss secondary overhangs."),
            enum_entry("support_base_pattern", {"rectilinear", "grid", "honeycomb"}, "Support Base Pattern", "Pattern changes removal force and stability."),
            enum_entry("support_interface_pattern", {"rectilinear", "concentric"}, "Support Interface Pattern", "Interface affects underside finish."),
            int_entry("support_interface_layers", 0, 10, "Support Interface Layers", "Too many layers can fuse to model."),
            float_entry("support_interface_spacing", 0.0, 1.0, "Support Interface Spacing", "Spacing controls density and removability."),
            float_entry("support_object_xy_distance", 0.0, 5.0, "Support XY Distance", "Too small can weld support to part."),
            float_entry("support_top_z_distance", 0.0, 2.0, "Support Top Z Distance", "Too large can degrade overhang quality."),
            float_entry("support_bottom_z_distance", 0.0, 2.0, "Support Bottom Z Distance", "Too small can reduce removability."),
            int_entry("support_angle", 0, 359, "Support Angle", "Angle affects support anisotropy."),
            float_entry("support_line_width", 0.20, 1.20, "Support Line Width", "Wide lines can overbuild support mass."),
            bool_entry("support_material_synchronize_layers", "Synchronize Support Layers", "Disabling may create timing mismatches in layer strategy."),
            int_entry("raft_layers", 0, 10, "Raft Layers", "Rafts increase material use and can warp dimensions."),
            enum_entry("brim_type", {"no_brim", "outer", "inner", "outer_and_inner"}, "Brim Type", "Brim can improve adhesion but affect removal."),
            float_entry("brim_width", 0.0, 20.0, "Brim Width", "Large brims consume area and can fuse to part base."),
            float_entry("brim_object_gap", 0.0, 5.0, "Brim Object Gap", "Small gaps may make brim hard to remove."),
            int_entry("skirt_loops", 0, 20, "Skirt Loops", "High loop count wastes time/material."),
            float_entry("skirt_distance", 0.0, 20.0, "Skirt Distance", "Distance impacts priming effectiveness."),
            enum_entry("draft_shield", {"disabled", "enabled", "full"}, "Draft Shield", "Draft shields can trap heat around the print."),
            bool_entry("ooze_prevention", "Ooze Prevention", "May increase travel and idle moves."),
            float_entry("outer_wall_speed", 1.0, 500.0, "Outer Wall Speed", "High speed can reduce surface quality."),
            float_entry("inner_wall_speed", 1.0, 500.0, "Inner Wall Speed", "High speed can reduce bonding consistency."),
            float_entry("sparse_infill_speed", 1.0, 500.0, "Sparse Infill Speed", "Extreme speed can under-extrude sparse regions."),
            float_entry("internal_solid_infill_speed", 1.0, 500.0, "Internal Solid Infill Speed", "Over-speeding can degrade top support."),
            float_entry("top_surface_speed", 1.0, 300.0, "Top Surface Speed", "Top surfaces are sensitive to speed artifacts."),
            float_entry("initial_layer_speed", 1.0, 200.0, "Initial Layer Speed", "Too high can hurt bed adhesion."),
            float_entry("initial_layer_infill_speed", 1.0, 200.0, "Initial Layer Infill Speed", "Initial infill speed influences first-layer bonding."),
            float_entry("travel_speed", 1.0, 1000.0, "Travel Speed", "Very high travel can induce skipped steps."),
            bool_entry("slow_down_for_layer_cooling", "Slow Down For Cooling", "Disabling may overheat small layers."),
            float_entry("min_print_speed", 1.0, 100.0, "Minimum Print Speed", "Too high minimum speed can defeat cooling safeguards."),
            float_entry("max_volumetric_speed", 0.5, 80.0, "Max Volumetric Speed", "Over-limit values can exceed melt capacity."),
            bool_entry("acceleration_enabled", "Enable Acceleration Control", "Acceleration overrides may conflict with firmware tuning."),
            int_entry("default_acceleration", 100, 30000, "Default Acceleration", "High acceleration increases ringing risk."),
            int_entry("outer_wall_acceleration", 100, 30000, "Outer Wall Acceleration", "Too high harms cosmetic surfaces."),
            int_entry("inner_wall_acceleration", 100, 30000, "Inner Wall Acceleration", "High values can destabilize tall parts."),
            int_entry("travel_acceleration", 100, 30000, "Travel Acceleration", "Aggressive travel acceleration can shift the print."),
            int_entry("bridge_acceleration", 100, 30000, "Bridge Acceleration", "Bridge quality is sensitive to jerk/accel."),
            bool_entry("jerk_enabled", "Enable Jerk Control", "Jerk overrides can cause vibration on weak frames."),
            float_entry("jerk_print", 1.0, 50.0, "Print Jerk", "High print jerk can scar walls."),
            float_entry("jerk_travel", 1.0, 50.0, "Travel Jerk", "High travel jerk can shake the model."),
            float_entry("retraction_length", 0.0, 20.0, "Retraction Length", "Excessive retraction can grind filament."),
            float_entry("retraction_speed", 1.0, 200.0, "Retraction Speed", "Too high speed can strip filament."),
            float_entry("deretraction_speed", 1.0, 200.0, "Deretraction Speed", "Too high speed can cause pressure spikes."),
            float_entry("retract_before_travel", 0.0, 20.0, "Retract Before Travel", "Low thresholds can increase stringing."),
            float_entry("retract_lift_z", 0.0, 5.0, "Z-Hop Height", "Large Z-hop values add print time and artifacts."),
            float_entry("retract_lift_above", 0.0, 400.0, "Z-Hop Lift Above", "Incorrect threshold may disable intended hops."),
            float_entry("retract_lift_below", 0.0, 400.0, "Z-Hop Lift Below", "Incorrect threshold may over-apply hops."),
            float_entry("retract_restart_extra", -2.0, 2.0, "Retraction Restart Extra", "Offsets can over/under-extrude seam starts."),
            bool_entry("wipe", "Wipe While Retracting", "Wipe can reduce ooze but adds path complexity."),
            float_entry("wipe_distance", 0.0, 20.0, "Wipe Distance", "Long wipe distances increase non-productive motion."),
            bool_entry("avoid_crossing_wall", "Avoid Crossing Wall", "Can significantly alter travel planning."),
            bool_entry("avoid_crossing_curled_overhangs", "Avoid Curled Overhangs", "Safety travel mode may increase route length."),
            enum_entry("z_hop_types", {"normal", "spiral", "smart"}, "Z-Hop Type", "Advanced modes may be firmware-sensitive."),
            float_entry("seam_gap", 0.0, 1.0, "Seam Gap", "Large seam gap can weaken perimeter continuity."),
            int_entry("nozzle_temperature", 150, 320, "Nozzle Temperature", "Out-of-range temperature can damage material or hotend."),
            int_entry("nozzle_temperature_initial_layer", 150, 340, "Initial Layer Nozzle Temperature", "Excessive first-layer heat can cause elephant foot."),
            int_entry("bed_temperature", 0, 130, "Bed Temperature", "Too high bed temperatures can deform parts."),
            int_entry("bed_temperature_initial_layer", 0, 130, "Initial Layer Bed Temperature", "Initial bed heat strongly impacts adhesion."),
            bool_entry("fan_always_on", "Fan Always On", "Always-on fan can weaken layer bonding on some materials."),
            bool_entry("cooling", "Enable Cooling", "Disabling cooling can overheat bridges and details."),
            int_entry("min_fan_speed", 0, 100, "Minimum Fan Speed", "Very high minimum fan may hurt interlayer strength."),
            int_entry("max_fan_speed", 0, 100, "Maximum Fan Speed", "Excessive fan can reduce adhesion."),
            int_entry("bridge_fan_speed", 0, 100, "Bridge Fan Speed", "Bridge cooling is material-dependent."),
            int_entry("disable_fan_first_layers", 0, 20, "Disable Fan For First Layers", "Too many layers without fan can soften geometry."),
            int_entry("overhang_fan_speed", 0, 100, "Overhang Fan Speed", "Aggressive cooling can induce warping."),
            int_entry("chamber_temperature", 0, 90, "Chamber Temperature", "High chamber setpoints can exceed material limits."),
            enum_entry("filament_type", {"pla", "petg", "abs", "asa", "pc", "pa", "tpu", "support"}, "Filament Type", "Type mismatch can invalidate thermal assumptions."),
            float_entry("filament_diameter", 1.0, 3.0, "Filament Diameter", "Incorrect diameter causes systematic flow errors."),
            float_entry("flow_ratio", 0.5, 1.5, "Flow Ratio", "Large deviations can cause weak or overfilled parts."),
            float_entry("pressure_advance", 0.0, 1.0, "Pressure Advance", "Requires firmware support and careful tuning."),
            float_entry("volumetric_flow_limit", 0.0, 100.0, "Volumetric Flow Limit", "Unrealistic limits can cause severe under/over extrusion."),
            enum_entry("gcode_flavor", {"marlin", "klipper", "reprap", "smoothie"}, "G-code Flavor", "Must match printer firmware for safe motion."),
            bool_entry("spiral_vase", "Spiral Vase", "Single-wall continuous mode is not suitable for all parts."),
            bool_entry("complete_objects", "Complete Objects", "Sequential printing risks collisions on tall models."),
            enum_entry("print_sequence", {"by_layer", "by_object"}, "Print Sequence", "By-object mode needs strict clearance checks."),
            bool_entry("only_retract_when_crossing_perimeters", "Only Retract When Crossing Perimeters", "Can reduce retractions but increase stringing inside infill."),
            string_entry("printer_notes", 0, 256, "Printer Notes", "Free text only; avoid injecting control sequences."),
            string_entry("filament_notes", 0, 256, "Filament Notes", "Free text metadata, not for executable content.")
        };

        std::unordered_map<std::string, AllowlistEntry> map;
        map.reserve(list.size());
        for (const AllowlistEntry& entry : list)
            map.emplace(entry.key, entry);
        return map;
    }();

    return entries;
}

} // namespace

bool AllowlistRegistry::is_allowed(const std::string& key)
{
    return registry().find(key) != registry().end();
}

ValidationResult AllowlistRegistry::validate_value(const std::string& key, const nlohmann::json& json_value)
{
    ValidationResult result;

    const auto it = registry().find(key);
    if (it == registry().end()) {
        result.error_message = "key is not allowlisted";
        return result;
    }

    const AllowlistEntry& entry = it->second;
    switch (entry.type) {
    case AllowlistValueType::Bool:
        if (!json_value.is_boolean()) {
            result.error_message = "expected bool";
            return result;
        }
        break;
    case AllowlistValueType::Int: {
        if (!json_value.is_number_integer()) {
            result.error_message = "expected int";
            return result;
        }
        if (!entry.has_numeric_bounds)
            break;
        const int value = json_value.get<int>();
        if (static_cast<double>(value) < entry.min_value || static_cast<double>(value) > entry.max_value) {
            result.error_message = "int out of bounds";
            return result;
        }
        break;
    }
    case AllowlistValueType::Float: {
        if (!json_value.is_number()) {
            result.error_message = "expected float";
            return result;
        }
        if (!entry.has_numeric_bounds)
            break;
        const double value = json_value.get<double>();
        if (value < entry.min_value || value > entry.max_value) {
            result.error_message = "float out of bounds";
            return result;
        }
        break;
    }
    case AllowlistValueType::String: {
        if (!json_value.is_string()) {
            result.error_message = "expected string";
            return result;
        }
        if (!entry.has_length_bounds)
            break;
        const auto& value = json_value.get_ref<const std::string&>();
        if (value.size() < entry.min_length || value.size() > entry.max_length) {
            result.error_message = "string length out of bounds";
            return result;
        }
        break;
    }
    case AllowlistValueType::Enum: {
        if (!json_value.is_string()) {
            result.error_message = "expected enum(string)";
            return result;
        }
        const auto& value = json_value.get_ref<const std::string&>();
        if (std::find(entry.enum_values.begin(), entry.enum_values.end(), value) == entry.enum_values.end()) {
            result.error_message = "enum value not allowed";
            return result;
        }
        break;
    }
    }

    result.valid = true;
    return result;
}

std::string AllowlistRegistry::label_for(const std::string& key)
{
    const auto it = registry().find(key);
    return it == registry().end() ? std::string() : it->second.label;
}

std::string AllowlistRegistry::safety_notes_for(const std::string& key)
{
    const auto it = registry().find(key);
    return it == registry().end() ? std::string() : it->second.safety_notes;
}

bool AllowlistRegistry::has_tag(const std::string& key, const std::string& tag)
{
    const auto it = registry().find(key);
    if (it == registry().end())
        return false;
    const auto& tags = it->second.tags;
    return std::find(tags.begin(), tags.end(), tag) != tags.end();
}

bool AllowlistRegistry::has_any_tag(const std::string& key, std::initializer_list<std::string> tags)
{
    const auto it = registry().find(key);
    if (it == registry().end())
        return false;
    for (const std::string& tag : tags) {
        if (std::find(it->second.tags.begin(), it->second.tags.end(), tag) != it->second.tags.end())
            return true;
    }
    return false;
}

size_t AllowlistRegistry::key_count()
{
    return registry().size();
}

} // namespace Apply
} // namespace AI
} // namespace Slic3r
