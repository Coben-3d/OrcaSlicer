#include "context_snapshot.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

#include "libslic3r/Config.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/libslic3r.h"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Plater.hpp"

namespace Slic3r {
namespace AI {

namespace {

using nlohmann::json;

std::string platform_name()
{
#ifdef __APPLE__
    return "macOS";
#elif defined(_WIN32)
    return "Windows";
#elif defined(__linux__)
    return "Linux";
#else
    return "Unknown";
#endif
}

json vec2_to_json(const Vec2d& v)
{
    return json{{"x", v.x()}, {"y", v.y()}};
}

json vec3_to_json(const Vec3d& v)
{
    return json{{"x", v.x()}, {"y", v.y()}, {"z", v.z()}};
}

std::string enum_value_to_string(const t_config_enum_values* keys_map, int value)
{
    if (keys_map == nullptr)
        return {};
    for (const auto& kv : *keys_map) {
        if (kv.second == value)
            return kv.first;
    }
    return {};
}

json float_or_percent_to_json(const FloatOrPercent& value)
{
    return json{{"value", value.value}, {"percent", value.percent}};
}

json serialize_floats(const ConfigOption* option)
{
    json arr = json::array();
    if (const auto* values = dynamic_cast<const ConfigOptionFloatsNullable*>(option)) {
        for (size_t i = 0; i < values->values.size(); ++i)
            arr.push_back(values->is_nil(i) ? json(nullptr) : json(values->values[i]));
        return arr;
    }
    if (const auto* values = dynamic_cast<const ConfigOptionFloats*>(option)) {
        for (double v : values->values)
            arr.push_back(v);
        return arr;
    }
    return option->serialize();
}

json serialize_ints(const ConfigOption* option)
{
    json arr = json::array();
    if (const auto* values = dynamic_cast<const ConfigOptionIntsNullable*>(option)) {
        for (size_t i = 0; i < values->values.size(); ++i)
            arr.push_back(values->is_nil(i) ? json(nullptr) : json(values->values[i]));
        return arr;
    }
    if (const auto* values = dynamic_cast<const ConfigOptionInts*>(option)) {
        for (int v : values->values)
            arr.push_back(v);
        return arr;
    }
    return option->serialize();
}

json serialize_bools(const ConfigOption* option)
{
    json arr = json::array();
    if (const auto* values = dynamic_cast<const ConfigOptionBoolsNullable*>(option)) {
        for (size_t i = 0; i < values->values.size(); ++i)
            arr.push_back(values->is_nil(i) ? json(nullptr) : json(values->get_at(i)));
        return arr;
    }
    if (const auto* values = dynamic_cast<const ConfigOptionBools*>(option)) {
        for (size_t i = 0; i < values->values.size(); ++i)
            arr.push_back(values->get_at(i));
        return arr;
    }
    return option->serialize();
}

json serialize_enum_ints(const ConfigOption* option)
{
    json arr = json::array();
    if (const auto* values = dynamic_cast<const ConfigOptionEnumsGenericNullable*>(option)) {
        for (size_t i = 0; i < values->values.size(); ++i) {
            if (values->is_nil(i)) {
                arr.push_back(nullptr);
                continue;
            }
            const std::string enum_name = enum_value_to_string(values->keys_map, values->values[i]);
            arr.push_back(enum_name.empty() ? json(values->values[i]) : json(enum_name));
        }
        return arr;
    }
    if (const auto* values = dynamic_cast<const ConfigOptionEnumsGeneric*>(option)) {
        for (int v : values->values) {
            const std::string enum_name = enum_value_to_string(values->keys_map, v);
            arr.push_back(enum_name.empty() ? json(v) : json(enum_name));
        }
        return arr;
    }
    return option->serialize();
}

json serialize_floats_or_percents(const ConfigOption* option)
{
    json arr = json::array();
    if (const auto* values = dynamic_cast<const ConfigOptionFloatsOrPercentsNullable*>(option)) {
        for (size_t i = 0; i < values->values.size(); ++i)
            arr.push_back(values->is_nil(i) ? json(nullptr) : float_or_percent_to_json(values->values[i]));
        return arr;
    }
    if (const auto* values = dynamic_cast<const ConfigOptionFloatsOrPercents*>(option)) {
        for (const auto& v : values->values)
            arr.push_back(float_or_percent_to_json(v));
        return arr;
    }
    return option->serialize();
}

json config_option_to_json(const ConfigOption& option)
{
    switch (option.type()) {
    case coFloat:
    case coPercent:
        return dynamic_cast<const ConfigOptionFloat&>(option).value;
    case coInt:
        return dynamic_cast<const ConfigOptionInt&>(option).value;
    case coBool:
        return dynamic_cast<const ConfigOptionBool&>(option).value;
    case coString:
        return dynamic_cast<const ConfigOptionString&>(option).value;
    case coEnum: {
        if (const auto* enum_opt = dynamic_cast<const ConfigOptionEnumGeneric*>(&option)) {
            const std::string enum_name = enum_value_to_string(enum_opt->keys_map, enum_opt->value);
            return enum_name.empty() ? json(enum_opt->value) : json(enum_name);
        }
        return option.getInt();
    }
    case coFloatOrPercent: {
        const auto& fp = dynamic_cast<const ConfigOptionFloatOrPercent&>(option);
        return json{{"value", fp.value}, {"percent", fp.percent}};
    }
    case coPoint:
        return vec2_to_json(dynamic_cast<const ConfigOptionPoint&>(option).value);
    case coPoint3:
        return vec3_to_json(dynamic_cast<const ConfigOptionPoint3&>(option).value);
    case coFloats:
    case coPercents:
        return serialize_floats(&option);
    case coInts:
        return serialize_ints(&option);
    case coBools:
        return serialize_bools(&option);
    case coStrings: {
        json arr = json::array();
        const auto& values = dynamic_cast<const ConfigOptionStrings&>(option).values;
        for (const auto& v : values)
            arr.push_back(v);
        return arr;
    }
    case coEnums:
        return serialize_enum_ints(&option);
    case coFloatsOrPercents:
        return serialize_floats_or_percents(&option);
    case coPoints: {
        json arr = json::array();
        const auto& values = dynamic_cast<const ConfigOptionPoints&>(option).values;
        for (const auto& pt : values)
            arr.push_back(vec2_to_json(pt));
        return arr;
    }
    case coPointsGroups: {
        json groups = json::array();
        const auto& values = dynamic_cast<const ConfigOptionPointsGroups&>(option).values;
        for (const auto& group : values) {
            json one_group = json::array();
            for (const auto& pt : group)
                one_group.push_back(vec2_to_json(pt));
            groups.push_back(std::move(one_group));
        }
        return groups;
    }
    case coIntsGroups: {
        json groups = json::array();
        const auto& values = dynamic_cast<const ConfigOptionIntsGroups&>(option).values;
        for (const auto& group : values) {
            json one_group = json::array();
            for (int v : group)
                one_group.push_back(v);
            groups.push_back(std::move(one_group));
        }
        return groups;
    }
    default:
        return option.serialize();
    }
}

void append_settings(const DynamicPrintConfig& config, json& settings, const std::string& prefix)
{
    t_config_option_keys keys = config.keys();
    std::sort(keys.begin(), keys.end());
    for (const auto& key : keys) {
        const ConfigOption* option = config.option(key);
        if (option == nullptr)
            continue;
        settings[prefix + key] = config_option_to_json(*option);
    }
}

void append_settings_until_limit(const DynamicPrintConfig& config, json& settings, const std::string& prefix, size_t min_count)
{
    if (settings.size() >= min_count)
        return;

    t_config_option_keys keys = config.keys();
    std::sort(keys.begin(), keys.end());
    for (const auto& key : keys) {
        if (settings.size() >= min_count)
            break;

        const ConfigOption* option = config.option(key);
        if (option == nullptr)
            continue;
        settings[prefix + key] = config_option_to_json(*option);
    }
}

void add_optional_config_value(const DynamicPrintConfig& config, const std::string& key, json& dst, const std::string& out_key)
{
    const ConfigOption* option = config.option(key);
    if (option != nullptr)
        dst[out_key] = config_option_to_json(*option);
}

void append_bed_size(const DynamicPrintConfig& config, json& printer_json)
{
    const auto* printable_area = config.option<ConfigOptionPoints>("printable_area");
    if (printable_area == nullptr || printable_area->values.empty())
        return;

    double min_x = std::numeric_limits<double>::max();
    double max_x = std::numeric_limits<double>::lowest();
    double min_y = std::numeric_limits<double>::max();
    double max_y = std::numeric_limits<double>::lowest();

    for (const auto& point : printable_area->values) {
        min_x = std::min(min_x, point.x());
        max_x = std::max(max_x, point.x());
        min_y = std::min(min_y, point.y());
        max_y = std::max(max_y, point.y());
    }

    if (min_x <= max_x && min_y <= max_y) {
        printer_json["bed_size"] = json{
            {"width", max_x - min_x},
            {"depth", max_y - min_y}
        };
    }
}

} // namespace

std::string build_context_snapshot_json(GUI::Plater& plater)
{
    json root = json::object();
    root["contract_version"] = "0.1.0";
    root["app"] = json{
        {"name", SLIC3R_APP_FULL_NAME},
        {"version", SLIC3R_VERSION},
        {"platform", platform_name()}
    };

    auto* bundle = GUI::wxGetApp().preset_bundle;
    if (bundle == nullptr)
        return root.dump(2);

    const bool is_fff = plater.printer_technology() == ptFFF;

    std::string filament_profile_name;
    if (is_fff) {
        filament_profile_name = bundle->filament_presets.empty() ? bundle->filaments.get_selected_preset_name() : bundle->filament_presets.front();
    } else {
        filament_profile_name = bundle->sla_materials.get_selected_preset_name();
    }

    root["profiles"] = json{
        {"printer_profile_name", bundle->printers.get_selected_preset_name()},
        {"filament_profile_name", filament_profile_name},
        {"print_profile_name", is_fff ? bundle->prints.get_selected_preset_name() : bundle->sla_prints.get_selected_preset_name()}
    };

    const DynamicPrintConfig& printer_config  = bundle->printers.get_edited_preset().config;
    const DynamicPrintConfig& filament_config = is_fff ? bundle->filaments.get_edited_preset().config : bundle->sla_materials.get_edited_preset().config;
    const DynamicPrintConfig  full_config     = bundle->full_config();

    json printer_json = json::object();
    add_optional_config_value(printer_config, "nozzle_diameter", printer_json, "nozzle_diameter");
    append_bed_size(printer_config, printer_json);
    if (const auto* model = printer_config.option<ConfigOptionString>("printer_model"); model != nullptr && !model->value.empty())
        printer_json["model"] = model->value;
    root["printer"] = std::move(printer_json);

    json filament_json = json::object();
    if (const auto* type = filament_config.option<ConfigOptionString>("filament_type"); type != nullptr && !type->value.empty())
        filament_json["type"] = type->value;
    add_optional_config_value(filament_config, "filament_diameter", filament_json, "diameter");

    json temps = json::object();
    add_optional_config_value(full_config, "nozzle_temperature", temps, "nozzle");
    add_optional_config_value(full_config, "nozzle_temperature_initial_layer", temps, "nozzle_initial_layer");
    add_optional_config_value(full_config, "bed_temperature", temps, "bed");
    add_optional_config_value(full_config, "bed_temperature_initial_layer", temps, "bed_initial_layer");
    if (!temps.empty())
        filament_json["temps"] = std::move(temps);
    root["filament"] = std::move(filament_json);

    json project_json = json{
        {"plate_count", plater.get_partplate_list().get_plate_count()},
        {"object_count", static_cast<int>(plater.model().objects.size())}
    };
    const int selected_object_idx = plater.get_selected_object_idx();
    if (selected_object_idx >= 0 && selected_object_idx < static_cast<int>(plater.model().objects.size())) {
        const ModelObject* object = plater.model().objects[static_cast<size_t>(selected_object_idx)];
        if (object != nullptr) {
            project_json["selected_object"] = json{
                {"index", selected_object_idx},
                {"name", object->name}
            };
        }
    }
    root["project"] = std::move(project_json);

    json settings = json::object();
    append_settings(full_config, settings, "");

    // Keep a safety fallback in case a constrained profile has too few keys.
    if (settings.size() < 100) {
        const DynamicPrintConfig& print_config = is_fff ? bundle->prints.get_edited_preset().config : bundle->sla_prints.get_edited_preset().config;
        append_settings_until_limit(printer_config, settings, "printer.", 100);
        append_settings_until_limit(filament_config, settings, "filament.", 100);
        append_settings_until_limit(bundle->project_config, settings, "project.", 100);
        append_settings_until_limit(print_config, settings, "print.", 100);
    }
    root["settings"] = std::move(settings);

    return root.dump(2);
}

} // namespace AI
} // namespace Slic3r
