#include "AISliceAssistantPanel.hpp"

#include "../../../ai/context_snapshot.h"
#include "../GUI_App.hpp"
#include "../Plater.hpp"

#include "libslic3r/Config.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "nlohmann/json.hpp"

#include <wx/event.h>
#include <wx/button.h>
#include <wx/checklst.h>
#include <wx/clipbrd.h>
#include <wx/dataobj.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/string.h>
#include <wx/textctrl.h>
#include <wx/utils.h>

#include <exception>
#include <iomanip>
#include <sstream>
#include <utility>

namespace Slic3r {
namespace GUI {

namespace {

using nlohmann::json;

struct PreparedOperation
{
    AISliceAssistantPanel::ApplyScope scope { AISliceAssistantPanel::ApplyScope::Global };
    int         object_idx { -1 };
    std::string key;
    std::string serialized_new_value;
    ConfigBase* target_config { nullptr };
};

bool scope_from_string(const std::string& scope, AISliceAssistantPanel::ApplyScope& out_scope)
{
    if (scope == "global") {
        out_scope = AISliceAssistantPanel::ApplyScope::Global;
        return true;
    }
    if (scope == "profile") {
        out_scope = AISliceAssistantPanel::ApplyScope::Profile;
        return true;
    }
    if (scope == "object") {
        out_scope = AISliceAssistantPanel::ApplyScope::Object;
        return true;
    }
    return false;
}

std::string serialize_number(double value)
{
    std::ostringstream oss;
    oss << std::setprecision(12) << value;
    return oss.str();
}

bool serialize_value_for_setting(const json& value, const std::string& value_type, std::string& out_serialized)
{
    if (value_type == "bool") {
        if (!value.is_boolean())
            return false;
        out_serialized = value.get<bool>() ? "1" : "0";
        return true;
    }
    if (value_type == "int") {
        if (!value.is_number_integer())
            return false;
        out_serialized = std::to_string(value.get<int>());
        return true;
    }
    if (value_type == "float") {
        if (!value.is_number())
            return false;
        out_serialized = serialize_number(value.get<double>());
        return true;
    }
    if (value_type == "string") {
        if (!value.is_string())
            return false;
        out_serialized = escape_string_cstyle(value.get<std::string>());
        return true;
    }
    if (value_type == "enum") {
        if (!value.is_string())
            return false;
        out_serialized = value.get<std::string>();
        return true;
    }

    return false;
}

std::string build_repair_request(const std::string& original_user_message,
                                 const std::string& invalid_output_json,
                                 const std::vector<std::string>& errors)
{
    std::ostringstream oss;
    oss << "repair_request\\n";
    oss << "instruction: output corrected JSON only\\n";
    oss << "target_contract_version: 0.1.0\\n";
    oss << "original_user_message:\\n" << original_user_message << "\\n";
    oss << "invalid_output:\\n" << invalid_output_json << "\\n";
    oss << "validation_errors:\\n";
    for (const std::string& err : errors)
        oss << "- " << err << "\\n";
    return oss.str();
}

} // namespace

AISliceAssistantPanel::AISliceAssistantPanel(wxWindow* parent)
    : wxPanel(parent, wxID_ANY)
{
    m_plater = dynamic_cast<Plater*>(parent);

    auto* root_sizer = new wxBoxSizer(wxVERTICAL);

    m_history = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE | wxTE_READONLY);

    auto* recommended_label = new wxStaticText(this, wxID_ANY, "Recommended changes");
    m_recommended_changes_list = new wxCheckListBox(this, wxID_ANY);
    m_recommended_changes_list->SetMinSize(wxSize(-1, FromDIP(130)));

    m_change_details = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE | wxTE_READONLY);
    m_change_details->SetMinSize(wxSize(-1, FromDIP(95)));

    m_apply   = new wxButton(this, wxID_ANY, "Apply");
    m_undo    = new wxButton(this, wxID_ANY, "Undo");

    m_input   = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
    m_send    = new wxButton(this, wxID_ANY, "Send");
    m_copy_context = new wxButton(this, wxID_ANY, "Copy Context");
    m_copy_last_json = new wxButton(this, wxID_ANY, "Copy Last JSON");

    auto* apply_row = new wxBoxSizer(wxHORIZONTAL);
    apply_row->Add(m_apply, 0, wxRIGHT, FromDIP(6));
    apply_row->Add(m_undo, 0);

    auto* input_row = new wxBoxSizer(wxHORIZONTAL);
    input_row->Add(m_input, 1, wxEXPAND | wxRIGHT, FromDIP(6));
    input_row->Add(m_send, 0, wxRIGHT, FromDIP(6));
    input_row->Add(m_copy_context, 0, wxRIGHT, FromDIP(6));
    input_row->Add(m_copy_last_json, 0, wxEXPAND);

    root_sizer->Add(m_history, 1, wxEXPAND | wxALL, FromDIP(8));
    root_sizer->Add(recommended_label, 0, wxLEFT | wxRIGHT, FromDIP(8));
    root_sizer->Add(m_recommended_changes_list, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(8));
    root_sizer->Add(m_change_details, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(8));
    root_sizer->Add(apply_row, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(8));
    root_sizer->Add(input_row, 0, wxEXPAND | wxALL, FromDIP(8));
    SetSizer(root_sizer);

    m_send->Bind(wxEVT_BUTTON, &AISliceAssistantPanel::on_send, this);
    m_input->Bind(wxEVT_TEXT_ENTER, &AISliceAssistantPanel::on_send, this);
    m_copy_context->Bind(wxEVT_BUTTON, &AISliceAssistantPanel::on_copy_context, this);
    m_copy_last_json->Bind(wxEVT_BUTTON, &AISliceAssistantPanel::on_copy_last_json, this);
    m_apply->Bind(wxEVT_BUTTON, &AISliceAssistantPanel::on_apply, this);
    m_undo->Bind(wxEVT_BUTTON, &AISliceAssistantPanel::on_undo, this);
    m_recommended_changes_list->Bind(wxEVT_LISTBOX, &AISliceAssistantPanel::on_change_list_event, this);
    m_recommended_changes_list->Bind(wxEVT_CHECKLISTBOX, &AISliceAssistantPanel::on_change_list_event, this);
}

void AISliceAssistantPanel::on_send(wxCommandEvent& event)
{
    wxUnusedVar(event);

    if (m_input == nullptr || m_history == nullptr)
        return;

    const wxString message = m_input->GetValue().Trim().Trim(false);
    if (message.empty())
        return;

    append_history_line("You: " + message);

    const Slic3r::AI::Providers::ProviderRequest request{
        message.ToStdString(),
        m_last_context_snapshot_json,
        m_last_geometry_insights_json
    };
    std::string provider_output = m_fake_provider.run(request);
    Slic3r::AI::Validation::ValidationResult validation = m_response_validator.validate(provider_output);

    bool repaired = false;
    int repairs_attempted = 0;
    while (!validation.valid && repairs_attempted < 2) {
        ++repairs_attempted;
        const Slic3r::AI::Providers::ProviderRequest repair_request{
            build_repair_request(message.ToStdString(), provider_output, validation.errors),
            m_last_context_snapshot_json,
            m_last_geometry_insights_json
        };
        provider_output = m_fake_provider.run(repair_request);
        validation = m_response_validator.validate(provider_output);
    }
    repaired = validation.valid && repairs_attempted > 0;

    m_last_ai_response_json = provider_output;
    if (validation.valid) {
        try {
            const nlohmann::json parsed = nlohmann::json::parse(m_last_ai_response_json);
            const std::string summary = parsed.value("summary", "Reponse provider recue.");
            append_history_line("Assistant: " + wxString::FromUTF8(summary.c_str()));
            populate_recommendations_from_response(parsed);
        } catch (...) {
            clear_recommendations();
            append_history_line("Assistant: reponse provider invalide.");
        }
        append_history_line(repaired ? "Repaired" : "Valid");
    } else {
        clear_recommendations();
        append_history_line("Rejected");
        for (const std::string& error : validation.errors)
            append_history_line("Validation error: " + wxString::FromUTF8(error.c_str()));
    }

    m_input->Clear();
}

void AISliceAssistantPanel::on_copy_context(wxCommandEvent& event)
{
    wxUnusedVar(event);

    if (m_plater == nullptr) {
        append_history_line("System: unavailable context (plater not found).");
        return;
    }

    m_last_context_snapshot_json = Slic3r::AI::build_context_snapshot_json(*m_plater);
    if (m_last_context_snapshot_json.empty()) {
        append_history_line("System: failed to generate context snapshot.");
        return;
    }

    if (wxTheClipboard->Open()) {
        wxTheClipboard->SetData(new wxTextDataObject(wxString::FromUTF8(m_last_context_snapshot_json.c_str())));
        wxTheClipboard->Close();
        append_history_line("System: context snapshot copied to clipboard.");
    } else {
        append_history_line("System: unable to access clipboard.");
    }
}

void AISliceAssistantPanel::on_copy_last_json(wxCommandEvent& event)
{
    wxUnusedVar(event);

    if (m_last_ai_response_json.empty()) {
        append_history_line("System: no provider JSON available yet.");
        return;
    }

    if (wxTheClipboard->Open()) {
        wxTheClipboard->SetData(new wxTextDataObject(wxString::FromUTF8(m_last_ai_response_json.c_str())));
        wxTheClipboard->Close();
        append_history_line("System: last provider JSON copied to clipboard.");
    } else {
        append_history_line("System: unable to access clipboard.");
    }
}

void AISliceAssistantPanel::on_apply(wxCommandEvent& event)
{
    wxUnusedVar(event);

    std::string error;
    if (apply_selected_changes_atomically(error)) {
        append_history_line("System: recommendations applied.");
    } else {
        append_history_line("System: apply failed - " + wxString::FromUTF8(error.c_str()));
    }
}

void AISliceAssistantPanel::on_undo(wxCommandEvent& event)
{
    wxUnusedVar(event);

    std::string error;
    if (undo_last_apply_atomically(error)) {
        append_history_line("System: undo completed.");
    } else {
        append_history_line("System: undo failed - " + wxString::FromUTF8(error.c_str()));
    }
}

void AISliceAssistantPanel::on_change_list_event(wxCommandEvent& event)
{
    update_change_details(event.GetInt());
}

void AISliceAssistantPanel::append_history_line(const wxString& line)
{
    if (m_history == nullptr)
        return;

    if (!m_history->IsEmpty())
        m_history->AppendText("\n");
    m_history->AppendText(line);
}

void AISliceAssistantPanel::clear_recommendations()
{
    m_recommended_changes.clear();
    if (m_recommended_changes_list)
        m_recommended_changes_list->Clear();
    if (m_change_details)
        m_change_details->Clear();
}

void AISliceAssistantPanel::populate_recommendations_from_response(const nlohmann::json& response_json)
{
    clear_recommendations();

    if (!response_json.contains("recommended_changes") || !response_json.at("recommended_changes").is_array())
        return;

    for (const auto& item : response_json.at("recommended_changes")) {
        if (!item.is_object())
            continue;

        RecommendedChange change;
        change.id                         = item.value("id", "");
        change.key                        = item.value("key", "");
        if (item.contains("value"))
            change.value = item.at("value");
        change.value_type                 = item.value("value_type", "");
        change.reason                     = item.value("reason", "");
        change.confidence                 = item.value("confidence", 0.0);
        change.applies_to                 = item.value("applies_to", "");
        change.requires_user_confirmation = item.value("requires_user_confirmation", false);
        if (item.contains("impact") && item.at("impact").is_object()) {
            change.quality = item.at("impact").value("quality", 0);
            change.time    = item.at("impact").value("time", 0);
            change.risk    = item.at("impact").value("risk", 0);
        }
        if (item.contains("tags") && item.at("tags").is_array()) {
            for (const auto& tag : item.at("tags")) {
                if (tag.is_string())
                    change.tags.push_back(tag.get<std::string>());
            }
        }

        if (change.key.empty())
            continue;

        m_recommended_changes.push_back(std::move(change));
    }

    for (size_t i = 0; i < m_recommended_changes.size(); ++i) {
        const auto& change = m_recommended_changes[i];
        const std::string label = Slic3r::AI::Apply::AllowlistRegistry::label_for(change.key);
        std::string display = label.empty() ? change.key : (label + " [" + change.key + "]");
        m_recommended_changes_list->Append(wxString::FromUTF8(display.c_str()));
        m_recommended_changes_list->Check(static_cast<unsigned int>(i), true);
    }

    if (!m_recommended_changes.empty()) {
        m_recommended_changes_list->SetSelection(0);
        update_change_details(0);
    }
}

void AISliceAssistantPanel::update_change_details(int index)
{
    if (m_change_details == nullptr)
        return;

    if (index < 0 || index >= static_cast<int>(m_recommended_changes.size())) {
        m_change_details->Clear();
        return;
    }

    const RecommendedChange& change = m_recommended_changes[static_cast<size_t>(index)];
    const std::string label = Slic3r::AI::Apply::AllowlistRegistry::label_for(change.key);
    const std::string safety = Slic3r::AI::Apply::AllowlistRegistry::safety_notes_for(change.key);

    std::ostringstream details;
    details << "Key: " << change.key << "\\n";
    if (!label.empty())
        details << "Label: " << label << "\\n";
    details << "Scope: " << change.applies_to << "\\n";
    details << "Type: " << change.value_type << "\\n";
    details << "Value: " << change.value.dump() << "\\n";
    details << "Reason: " << change.reason << "\\n";
    details << "Impact (Q/T/R): " << change.quality << "/" << change.time << "/" << change.risk << "\\n";
    details << "Confidence: " << std::setprecision(3) << change.confidence << "\\n";
    details << "User confirmation: " << (change.requires_user_confirmation ? "yes" : "no") << "\\n";
    if (!change.tags.empty()) {
        details << "Tags: ";
        for (size_t i = 0; i < change.tags.size(); ++i) {
            if (i > 0)
                details << ", ";
            details << change.tags[i];
        }
        details << "\\n";
    }
    if (!safety.empty())
        details << "Safety: " << safety << "\\n";

    m_change_details->SetValue(wxString::FromUTF8(details.str().c_str()));
}

bool AISliceAssistantPanel::apply_selected_changes_atomically(std::string& error_message)
{
    if (m_plater == nullptr) {
        error_message = "plater unavailable";
        return false;
    }

    if (m_recommended_changes.empty()) {
        error_message = "no recommendations to apply";
        return false;
    }

    std::vector<PreparedOperation> prepared;
    prepared.reserve(m_recommended_changes.size());

    auto* bundle = wxGetApp().preset_bundle;
    if (bundle == nullptr) {
        error_message = "preset bundle unavailable";
        return false;
    }

    const int selected_object_idx = m_plater->get_selected_object_idx();

    for (size_t i = 0; i < m_recommended_changes.size(); ++i) {
        if (!m_recommended_changes_list->IsChecked(static_cast<unsigned int>(i)))
            continue;

        const RecommendedChange& change = m_recommended_changes[i];

        if (!Slic3r::AI::Apply::AllowlistRegistry::is_allowed(change.key)) {
            error_message = "key not allowed: " + change.key;
            return false;
        }
        if (!Slic3r::AI::Apply::AllowlistRegistry::validate_value(change.key, change.value)) {
            error_message = "invalid value for key: " + change.key;
            return false;
        }

        ApplyScope scope;
        if (!scope_from_string(change.applies_to, scope)) {
            error_message = "unknown scope: " + change.applies_to;
            return false;
        }

        ConfigBase* target_config = nullptr;
        int object_idx = -1;
        if (scope == ApplyScope::Global) {
            target_config = &bundle->project_config;
        } else if (scope == ApplyScope::Profile) {
            target_config = (m_plater->printer_technology() == ptFFF)
                ? static_cast<ConfigBase*>(&bundle->prints.get_edited_preset().config)
                : static_cast<ConfigBase*>(&bundle->sla_prints.get_edited_preset().config);
        } else {
            object_idx = selected_object_idx;
            if (object_idx < 0 || object_idx >= static_cast<int>(m_plater->model().objects.size())) {
                error_message = "object scope requested but no object is selected";
                return false;
            }
            ModelObject* object = m_plater->model().objects[static_cast<size_t>(object_idx)];
            if (object == nullptr) {
                error_message = "selected object not available";
                return false;
            }
            target_config = static_cast<ConfigBase*>(const_cast<DynamicPrintConfig*>(&object->config.get()));
        }

        if (target_config == nullptr || !target_config->has(change.key)) {
            error_message = "target does not support key: " + change.key;
            return false;
        }

        std::string serialized_new;
        if (!serialize_value_for_setting(change.value, change.value_type, serialized_new)) {
            error_message = "cannot serialize value for key: " + change.key;
            return false;
        }

        bool merged = false;
        for (PreparedOperation& existing : prepared) {
            if (existing.scope == scope && existing.object_idx == object_idx && existing.key == change.key) {
                existing.serialized_new_value = serialized_new;
                merged = true;
                break;
            }
        }
        if (!merged) {
            prepared.push_back(PreparedOperation{scope, object_idx, change.key, serialized_new, target_config});
        }
    }

    if (prepared.empty()) {
        error_message = "no checked changes";
        return false;
    }

    std::vector<AppliedValueBackup> backups;
    backups.reserve(prepared.size());
    for (const PreparedOperation& op : prepared) {
        const ConfigOption* original = op.target_config->option(op.key);
        if (original == nullptr) {
            error_message = "cannot snapshot key: " + op.key;
            return false;
        }
        backups.push_back(AppliedValueBackup{op.scope, op.object_idx, op.key, original->serialize()});
    }

    for (const PreparedOperation& op : prepared) {
        try {
            op.target_config->set_deserialize_strict(op.key, op.serialized_new_value);
        } catch (const std::exception& ex) {
            for (const AppliedValueBackup& backup : backups) {
                ConfigBase* rollback_config = nullptr;
                if (backup.scope == ApplyScope::Global) {
                    rollback_config = &bundle->project_config;
                } else if (backup.scope == ApplyScope::Profile) {
                    rollback_config = (m_plater->printer_technology() == ptFFF)
                        ? static_cast<ConfigBase*>(&bundle->prints.get_edited_preset().config)
                        : static_cast<ConfigBase*>(&bundle->sla_prints.get_edited_preset().config);
                } else if (backup.object_idx >= 0 && backup.object_idx < static_cast<int>(m_plater->model().objects.size())) {
                    ModelObject* object = m_plater->model().objects[static_cast<size_t>(backup.object_idx)];
                    if (object != nullptr)
                        rollback_config = static_cast<ConfigBase*>(const_cast<DynamicPrintConfig*>(&object->config.get()));
                }
                if (rollback_config != nullptr) {
                    try {
                        rollback_config->set_deserialize_strict(backup.key, backup.serialized_value);
                    } catch (...) {}
                }
            }
            error_message = std::string("apply failed on key '") + op.key + "': " + ex.what();
            return false;
        }
    }

    m_last_apply_backups = std::move(backups);
    m_last_apply_touched_global_or_profile = false;
    m_last_apply_touched_object = false;
    m_last_apply_object_idx = -1;

    for (const PreparedOperation& op : prepared) {
        if (op.scope == ApplyScope::Object) {
            m_last_apply_touched_object = true;
            m_last_apply_object_idx = op.object_idx;
        } else {
            m_last_apply_touched_global_or_profile = true;
        }
    }

    refresh_plater_after_changes(m_last_apply_touched_global_or_profile, m_last_apply_touched_object, m_last_apply_object_idx);
    return true;
}

bool AISliceAssistantPanel::undo_last_apply_atomically(std::string& error_message)
{
    if (m_plater == nullptr) {
        error_message = "plater unavailable";
        return false;
    }
    if (m_last_apply_backups.empty()) {
        error_message = "no applied snapshot to undo";
        return false;
    }

    auto* bundle = wxGetApp().preset_bundle;
    if (bundle == nullptr) {
        error_message = "preset bundle unavailable";
        return false;
    }

    std::vector<std::pair<ConfigBase*, AppliedValueBackup>> targets;
    targets.reserve(m_last_apply_backups.size());

    for (const AppliedValueBackup& backup : m_last_apply_backups) {
        ConfigBase* target_config = nullptr;
        if (backup.scope == ApplyScope::Global) {
            target_config = &bundle->project_config;
        } else if (backup.scope == ApplyScope::Profile) {
            target_config = (m_plater->printer_technology() == ptFFF)
                ? static_cast<ConfigBase*>(&bundle->prints.get_edited_preset().config)
                : static_cast<ConfigBase*>(&bundle->sla_prints.get_edited_preset().config);
        } else {
            if (backup.object_idx < 0 || backup.object_idx >= static_cast<int>(m_plater->model().objects.size())) {
                error_message = "object snapshot no longer valid";
                return false;
            }
            ModelObject* object = m_plater->model().objects[static_cast<size_t>(backup.object_idx)];
            if (object == nullptr) {
                error_message = "object snapshot missing";
                return false;
            }
            target_config = static_cast<ConfigBase*>(const_cast<DynamicPrintConfig*>(&object->config.get()));
        }

        if (target_config == nullptr || !target_config->has(backup.key)) {
            error_message = "cannot restore key: " + backup.key;
            return false;
        }

        targets.emplace_back(target_config, backup);
    }

    for (const auto& pair : targets) {
        try {
            pair.first->set_deserialize_strict(pair.second.key, pair.second.serialized_value);
        } catch (const std::exception& ex) {
            error_message = std::string("restore failed on key '") + pair.second.key + "': " + ex.what();
            return false;
        }
    }

    refresh_plater_after_changes(m_last_apply_touched_global_or_profile, m_last_apply_touched_object, m_last_apply_object_idx);
    m_last_apply_backups.clear();
    m_last_apply_touched_global_or_profile = false;
    m_last_apply_touched_object = false;
    m_last_apply_object_idx = -1;
    return true;
}

void AISliceAssistantPanel::refresh_plater_after_changes(bool touched_global_or_profile, bool touched_object, int object_idx)
{
    if (m_plater == nullptr)
        return;

    if (touched_global_or_profile && wxGetApp().preset_bundle != nullptr)
        m_plater->on_config_change(wxGetApp().preset_bundle->full_config());

    if (touched_object)
        m_plater->changed_object(object_idx);
}

} // namespace GUI
} // namespace Slic3r
