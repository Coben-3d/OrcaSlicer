#include "AISliceAssistantPanel.hpp"

#include "../../../ai/context_snapshot.h"
#include "../../../ai/providers/openai_compat/openai_compat_provider.h"
#include "../GUI_App.hpp"
#include "../MainFrame.hpp"
#include "../ParamsPanel.hpp"
#include "../Plater.hpp"
#include "../Tab.hpp"

#include "libslic3r/AppConfig.hpp"
#include "libslic3r/Config.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r_version.h"
#include "nlohmann/json.hpp"

#include <wx/event.h>
#include <wx/button.h>
#include <wx/clipbrd.h>
#include <wx/dataobj.h>
#include <wx/dataview.h>
#include <wx/filedlg.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/string.h>
#include <wx/textctrl.h>
#include <wx/utils.h>
#include <wx/wrapsizer.h>

#include <exception>
#include <fstream>
#include <iomanip>
#include <cctype>
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

std::string trim_copy(const std::string& value)
{
    size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin])))
        ++begin;

    size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])))
        --end;

    return value.substr(begin, end - begin);
}

bool is_missing_preset_name(const std::string& value)
{
    const std::string trimmed = trim_copy(value);
    if (trimmed.empty())
        return true;

    std::string lower = trimmed;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lower == "unknown" || lower == "n/a" || lower == "none";
}

void open_preset_tab(Slic3r::Preset::Type type)
{
    if (wxGetApp().mainframe != nullptr)
        wxGetApp().mainframe->select_tab(size_t(MainFrame::tp3DEditor));

    if (wxGetApp().params_panel() != nullptr)
        wxGetApp().params_panel()->switch_to_global();

    if (Tab* tab = wxGetApp().get_tab(type))
        tab->restore_last_select_item();
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

std::string compact_json_value(const json& value)
{
    std::string dumped = value.dump();
    constexpr size_t max_size = 56;
    if (dumped.size() <= max_size)
        return dumped;
    return dumped.substr(0, max_size - 3) + "...";
}

std::string compact_status_reason(const std::string& reason)
{
    constexpr size_t max_size = 40;
    if (reason.size() <= max_size)
        return reason;
    return reason.substr(0, max_size - 3) + "...";
}

bool is_safe_mode_enabled()
{
    const Slic3r::AppConfig* app_config = wxGetApp().app_config;
    if (app_config == nullptr)
        return true;

    const std::string configured = app_config->get("ai_safe_mode");
    if (configured.empty())
        return true;
    return app_config->get_bool("ai_safe_mode");
}

json parse_json_or_string(const std::string& raw_json)
{
    if (raw_json.empty())
        return json();
    try {
        return json::parse(raw_json);
    } catch (...) {
        return raw_json;
    }
}

long parse_long_with_fallback(const std::string& value, long fallback, long min_value, long max_value)
{
    if (value.empty())
        return fallback;
    try {
        long parsed = std::stol(value);
        if (parsed < min_value)
            return min_value;
        if (parsed > max_value)
            return max_value;
        return parsed;
    } catch (...) {
        return fallback;
    }
}

int parse_int_with_fallback(const std::string& value, int fallback, int min_value, int max_value)
{
    if (value.empty())
        return fallback;
    try {
        int parsed = std::stoi(value);
        if (parsed < min_value)
            return min_value;
        if (parsed > max_value)
            return max_value;
        return parsed;
    } catch (...) {
        return fallback;
    }
}

double parse_double_with_fallback(const std::string& value, double fallback, double min_value, double max_value)
{
    if (value.empty())
        return fallback;
    try {
        double parsed = std::stod(value);
        if (parsed < min_value)
            return min_value;
        if (parsed > max_value)
            return max_value;
        return parsed;
    } catch (...) {
        return fallback;
    }
}

std::string load_ai_provider_api_key(const Slic3r::AppConfig* app_config)
{
    if (app_config == nullptr)
        return {};

    std::string api_key = app_config->get("ai_provider_api_key");
    if (!api_key.empty())
        return api_key;

    if (app_config->get("ai_provider_api_key_storage") == "keychain") {
        std::string secure_error;
        if (Slic3r::AI::Providers::OpenAICompatProvider::load_api_key_securely(api_key, secure_error))
            return api_key;
    }

    return {};
}

std::string run_selected_provider(const Slic3r::AI::Providers::ProviderRequest& request,
                                  Slic3r::AI::Providers::FakeProvider& fake_provider,
                                  wxString& provider_name)
{
    provider_name = "Fake";
    Slic3r::AppConfig* app_config = wxGetApp().app_config;
    if (app_config == nullptr)
        return fake_provider.run(request);

    const std::string provider_type = app_config->get("ai_provider_type");
    if (provider_type != "openai_compat")
        return fake_provider.run(request);

    Slic3r::AI::Providers::OpenAICompatConfig openai_config;
    openai_config.provider_type   = provider_type;
    openai_config.base_url        = app_config->get("ai_provider_base_url");
    openai_config.model           = app_config->get("ai_provider_model");
    openai_config.timeout_seconds = parse_long_with_fallback(app_config->get("ai_provider_timeout_seconds"), 30, 5, 300);
    openai_config.max_tokens      = parse_int_with_fallback(app_config->get("ai_provider_max_tokens"), 600, 1, 4096);
    openai_config.temperature     = parse_double_with_fallback(app_config->get("ai_provider_temperature"), 0.2, 0.0, 2.0);
    openai_config.api_key         = load_ai_provider_api_key(app_config);
    openai_config.use_json_schema_response_format =
        app_config->get("ai_provider_use_json_schema").empty() || app_config->get_bool("ai_provider_use_json_schema");

    provider_name = "OpenAI-compatible";
    Slic3r::AI::Providers::OpenAICompatProvider openai_provider(std::move(openai_config));
    return openai_provider.run(request);
}

} // namespace

AISliceAssistantPanel::AISliceAssistantPanel(wxWindow* parent)
    : wxPanel(parent, wxID_ANY)
{
    m_plater = dynamic_cast<Plater*>(parent);

    auto* root_sizer = new wxBoxSizer(wxVERTICAL);

    m_context_card = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_SIMPLE);
    auto* context_card_sizer = new wxBoxSizer(wxVERTICAL);
    auto* context_title = new wxStaticText(m_context_card, wxID_ANY, "Context");
    m_context_summary = new wxStaticText(m_context_card, wxID_ANY, "");
    m_context_warning = new wxStaticText(m_context_card, wxID_ANY, "");
    m_change_printer = new wxButton(m_context_card, wxID_ANY, "Change printer");
    m_change_filament = new wxButton(m_context_card, wxID_ANY, "Change filament");
    m_start_over = new wxButton(m_context_card, wxID_ANY, "Start over");

    auto* context_actions = new wxBoxSizer(wxHORIZONTAL);
    context_actions->Add(m_change_printer, 0, wxRIGHT, FromDIP(6));
    context_actions->Add(m_change_filament, 0, wxRIGHT, FromDIP(6));
    context_actions->AddStretchSpacer(1);
    context_actions->Add(m_start_over, 0);

    context_card_sizer->Add(context_title, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(8));
    context_card_sizer->Add(m_context_summary, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(8));
    context_card_sizer->Add(m_context_warning, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(8));
    context_card_sizer->Add(context_actions, 0, wxEXPAND | wxALL, FromDIP(8));
    m_context_card->SetSizer(context_card_sizer);

    m_history = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE | wxTE_READONLY);

    auto* recommended_label = new wxStaticText(this, wxID_ANY, "Recommended changes");
    m_recommended_changes_list = new wxDataViewListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxDV_ROW_LINES | wxDV_VERT_RULES);
    m_recommended_changes_list->AppendToggleColumn("Enabled", wxDATAVIEW_CELL_ACTIVATABLE, FromDIP(76), wxALIGN_CENTER, wxDATAVIEW_COL_RESIZABLE);
    m_recommended_changes_list->AppendTextColumn("Setting", wxDATAVIEW_CELL_INERT, FromDIP(240), wxALIGN_LEFT, wxDATAVIEW_COL_RESIZABLE);
    m_recommended_changes_list->AppendTextColumn("Value", wxDATAVIEW_CELL_INERT, FromDIP(160), wxALIGN_LEFT, wxDATAVIEW_COL_RESIZABLE);
    m_recommended_changes_list->AppendTextColumn("Status", wxDATAVIEW_CELL_INERT, FromDIP(200), wxALIGN_LEFT, wxDATAVIEW_COL_RESIZABLE);
    m_recommended_changes_list->SetMinSize(wxSize(-1, FromDIP(130)));

    m_change_details = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE | wxTE_READONLY);
    m_change_details->SetMinSize(wxSize(-1, FromDIP(95)));

    m_apply   = new wxButton(this, wxID_ANY, "Apply Selected");
    m_undo    = new wxButton(this, wxID_ANY, "Undo Last Apply");

    m_input   = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE | wxTE_PROCESS_ENTER);
    m_input->SetMinSize(wxSize(-1, FromDIP(80)));
    m_input->Enable(true);
    m_input->SetEditable(true);
    m_input->Raise();
    m_send    = new wxButton(this, wxID_ANY, "Send");
    m_copy_context = new wxButton(this, wxID_ANY, "Copy Context");
    m_copy_last_json = new wxButton(this, wxID_ANY, "Copy Last JSON");
    m_export_debug = new wxButton(this, wxID_ANY, "Export Debug Bundle");

    const wxSize button_min_size = wxSize(FromDIP(110), -1);
    m_send->SetMinSize(button_min_size);
    m_copy_context->SetMinSize(button_min_size);
    m_copy_last_json->SetMinSize(button_min_size);
    m_apply->SetMinSize(button_min_size);
    m_undo->SetMinSize(button_min_size);
    m_export_debug->SetMinSize(wxSize(FromDIP(145), -1));
    m_change_printer->SetMinSize(button_min_size);
    m_change_filament->SetMinSize(button_min_size);
    m_start_over->SetMinSize(button_min_size);

    auto* bottom_area = new wxBoxSizer(wxVERTICAL);
    auto* buttons_wrap = new wxWrapSizer(wxHORIZONTAL);
    buttons_wrap->Add(m_send, 0, wxALL, FromDIP(3));
    buttons_wrap->Add(m_copy_context, 0, wxALL, FromDIP(3));
    buttons_wrap->Add(m_copy_last_json, 0, wxALL, FromDIP(3));
    buttons_wrap->Add(m_export_debug, 0, wxALL, FromDIP(3));
    buttons_wrap->Add(m_apply, 0, wxALL, FromDIP(3));
    buttons_wrap->Add(m_undo, 0, wxALL, FromDIP(3));

    bottom_area->Add(m_input, 1, wxEXPAND | wxALL, FromDIP(6));
    bottom_area->Add(buttons_wrap, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(3));

    root_sizer->Add(m_context_card, 0, wxEXPAND | wxALL, FromDIP(8));
    root_sizer->Add(m_history, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8));
    root_sizer->Add(recommended_label, 0, wxLEFT | wxRIGHT, FromDIP(8));
    root_sizer->Add(m_recommended_changes_list, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(8));
    root_sizer->Add(m_change_details, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(8));
    root_sizer->Add(bottom_area, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8));
    SetSizer(root_sizer);
    Layout();

    m_send->Bind(wxEVT_BUTTON, &AISliceAssistantPanel::on_send, this);
    m_input->Bind(wxEVT_TEXT_ENTER, &AISliceAssistantPanel::on_send, this);
    m_input->Bind(wxEVT_CHAR_HOOK, &AISliceAssistantPanel::on_input_char_hook, this);
    m_copy_context->Bind(wxEVT_BUTTON, &AISliceAssistantPanel::on_copy_context, this);
    m_copy_last_json->Bind(wxEVT_BUTTON, &AISliceAssistantPanel::on_copy_last_json, this);
    m_export_debug->Bind(wxEVT_BUTTON, &AISliceAssistantPanel::on_export_debug_bundle, this);
    m_change_printer->Bind(wxEVT_BUTTON, &AISliceAssistantPanel::on_change_printer, this);
    m_change_filament->Bind(wxEVT_BUTTON, &AISliceAssistantPanel::on_change_filament, this);
    m_start_over->Bind(wxEVT_BUTTON, &AISliceAssistantPanel::on_start_over, this);
    m_apply->Bind(wxEVT_BUTTON, &AISliceAssistantPanel::on_apply, this);
    m_undo->Bind(wxEVT_BUTTON, &AISliceAssistantPanel::on_undo, this);
    m_recommended_changes_list->Bind(wxEVT_DATAVIEW_SELECTION_CHANGED, &AISliceAssistantPanel::on_change_list_event, this);
    m_recommended_changes_list->Bind(wxEVT_DATAVIEW_ITEM_VALUE_CHANGED, &AISliceAssistantPanel::on_change_list_event, this);
    Bind(wxEVT_SHOW, &AISliceAssistantPanel::on_panel_show, this);
    Bind(wxEVT_SIZE, &AISliceAssistantPanel::on_panel_size, this);

    CallAfter([this]() {
        refresh_context_card();
        if (m_input != nullptr) {
            m_input->Enable(true);
            m_input->SetEditable(true);
            Layout();
            m_input->SetFocus();
        }
    });
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
    wxString provider_name;
    std::string provider_output = run_selected_provider(request, m_fake_provider, provider_name);
    Slic3r::AI::Validation::ValidationResult validation = m_response_validator.validate(provider_output);
    m_last_validation_errors.clear();
    if (!validation.valid)
        m_last_validation_errors.insert(m_last_validation_errors.end(), validation.errors.begin(), validation.errors.end());

    bool repaired = false;
    int repairs_attempted = 0;
    while (!validation.valid && repairs_attempted < 2) {
        ++repairs_attempted;
        const Slic3r::AI::Providers::ProviderRequest repair_request{
            build_repair_request(message.ToStdString(), provider_output, validation.errors),
            m_last_context_snapshot_json,
            m_last_geometry_insights_json
        };
        provider_output = run_selected_provider(repair_request, m_fake_provider, provider_name);
        validation = m_response_validator.validate(provider_output);
        if (!validation.valid)
            m_last_validation_errors.insert(m_last_validation_errors.end(), validation.errors.begin(), validation.errors.end());
    }
    repaired = validation.valid && repairs_attempted > 0;

    m_last_ai_response_json = provider_output;
    if (validation.valid) {
        try {
            const nlohmann::json parsed = nlohmann::json::parse(m_last_ai_response_json);
            const std::string summary = parsed.value("summary", "Reponse provider recue.");
            append_history_line("Assistant (" + provider_name + "): " + wxString::FromUTF8(summary.c_str()));
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
    refresh_context_card();
}

void AISliceAssistantPanel::on_input_char_hook(wxKeyEvent& event)
{
    if (m_input == nullptr) {
        event.Skip();
        return;
    }

    const int key_code = event.GetKeyCode();
    const bool is_enter = key_code == WXK_RETURN || key_code == WXK_NUMPAD_ENTER;
    if (!is_enter) {
        event.Skip();
        return;
    }

    const bool is_multiline = (m_input->GetWindowStyleFlag() & wxTE_MULTILINE) != 0;
    const bool shift_down = event.ShiftDown();
#ifdef __WXOSX__
    const bool cmd_or_ctrl_down = event.CmdDown();
#else
    const bool cmd_or_ctrl_down = event.ControlDown();
#endif

    if (cmd_or_ctrl_down || !is_multiline || !shift_down) {
        wxCommandEvent send_event(wxEVT_BUTTON, m_send ? m_send->GetId() : wxID_ANY);
        send_event.SetEventObject(m_send);
        on_send(send_event);
        return;
    }

    event.Skip();
}

void AISliceAssistantPanel::on_panel_show(wxShowEvent& event)
{
    refresh_context_card();
    if (event.IsShown() && m_input != nullptr) {
        m_input->Enable(true);
        m_input->SetEditable(true);
        m_input->Raise();
        Layout();
        CallAfter([this]() {
            if (m_input != nullptr) {
                Layout();
                m_input->SetFocus();
            }
        });
    }
    event.Skip();
}

void AISliceAssistantPanel::on_panel_size(wxSizeEvent& event)
{
    Layout();
    event.Skip();
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
    refresh_context_card();
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
    refresh_context_card();
}

void AISliceAssistantPanel::on_export_debug_bundle(wxCommandEvent& event)
{
    wxUnusedVar(event);

    wxFileDialog save_dialog(
        this,
        "Export AI Debug Bundle",
        "",
        "ai_debug_bundle.json",
        "JSON files (*.json)|*.json|All files (*.*)|*.*",
        wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (save_dialog.ShowModal() != wxID_OK)
        return;

    nlohmann::json bundle = nlohmann::json::object();
    bundle["last_context_snapshot_json"] = parse_json_or_string(m_last_context_snapshot_json);
    bundle["last_geometry_insights_json"] = parse_json_or_string(m_last_geometry_insights_json);
    bundle["last_ai_response_json"] = parse_json_or_string(m_last_ai_response_json);
    bundle["validation_errors"] = m_last_validation_errors;
    bundle["app"] = nlohmann::json::object({
        {"name", SLIC3R_APP_NAME},
        {"version", SLIC3R_VERSION},
        {"platform", wxGetOsDescription().ToStdString()}
    });

    const wxString path = save_dialog.GetPath();
    std::ofstream out(path.ToStdString(), std::ios::out | std::ios::trunc);
    if (!out.good()) {
        append_history_line("System: failed to write debug bundle file.");
        return;
    }
    out << bundle.dump(2);
    out.close();
    append_history_line("System: debug bundle exported to " + path);
    refresh_context_card();
}

void AISliceAssistantPanel::on_change_printer(wxCommandEvent& event)
{
    wxUnusedVar(event);
    open_preset_tab(Preset::TYPE_PRINTER);
}

void AISliceAssistantPanel::on_change_filament(wxCommandEvent& event)
{
    wxUnusedVar(event);
    open_preset_tab(Preset::TYPE_FILAMENT);
}

void AISliceAssistantPanel::on_start_over(wxCommandEvent& event)
{
    wxUnusedVar(event);

    m_last_context_snapshot_json.clear();
    m_last_geometry_insights_json.clear();
    m_last_ai_response_json.clear();
    m_last_validation_errors.clear();

    clear_recommendations();
    if (m_history != nullptr)
        m_history->Clear();
    if (m_input != nullptr) {
        m_input->Clear();
        m_input->SetFocus();
    }

    refresh_context_card();
}

void AISliceAssistantPanel::on_apply(wxCommandEvent& event)
{
    wxUnusedVar(event);

    size_t applied_count = 0;
    std::vector<std::string> errors;
    if (apply_selected_changes_atomically(applied_count, errors)) {
        append_history_line(wxString::Format("Applied %u changes", static_cast<unsigned int>(applied_count)));
    } else {
        append_history_line("Error list:");
        for (const std::string& err : errors)
            append_history_line("- " + wxString::FromUTF8(err.c_str()));
    }
    refresh_context_card();
}

void AISliceAssistantPanel::on_undo(wxCommandEvent& event)
{
    wxUnusedVar(event);

    std::string error;
    if (undo_last_apply_atomically(error)) {
        append_history_line("Undo successful");
    } else {
        append_history_line("Error list:");
        append_history_line("- " + wxString::FromUTF8(error.c_str()));
    }
    refresh_context_card();
}

void AISliceAssistantPanel::on_change_list_event(wxDataViewEvent& event)
{
    int index = wxNOT_FOUND;
    if (event.GetItem().IsOk())
        index = m_recommended_changes_list->ItemToRow(event.GetItem());
    if (index == wxNOT_FOUND)
        index = m_recommended_changes_list->GetSelectedRow();
    update_change_details(index);
    event.Skip();
}

void AISliceAssistantPanel::append_history_line(const wxString& line)
{
    if (m_history == nullptr)
        return;

    if (!m_history->IsEmpty())
        m_history->AppendText("\n");
    m_history->AppendText(line);
}

void AISliceAssistantPanel::refresh_context_card()
{
    if (m_context_summary == nullptr || m_context_warning == nullptr || m_context_card == nullptr)
        return;

    std::string printer_name = "Unknown";
    std::string filament_name = "Unknown";
    std::string process_name = "Unknown";

    bool printer_missing = true;
    bool filament_missing = true;

    const auto* bundle = wxGetApp().preset_bundle;
    if (bundle != nullptr) {
        printer_name = bundle->printers.get_edited_preset().name;
        if (is_missing_preset_name(printer_name))
            printer_name = "Unknown";
        printer_missing = (printer_name == "Unknown");

        for (const std::string& candidate : bundle->filament_presets) {
            if (!is_missing_preset_name(candidate)) {
                filament_name = candidate;
                break;
            }
        }
        if (is_missing_preset_name(filament_name))
            filament_name = "Unknown";
        filament_missing = (filament_name == "Unknown");

        process_name = bundle->prints.get_edited_preset().name;
        if (is_missing_preset_name(process_name))
            process_name = bundle->sla_prints.get_edited_preset().name;
        if (is_missing_preset_name(process_name))
            process_name = "Unknown";
    }

    std::ostringstream summary;
    summary << "Printer: " << printer_name << "\n";
    summary << "Filament: " << filament_name << "\n";
    summary << "Preset: " << process_name;
    m_context_summary->SetLabel(wxString::FromUTF8(summary.str().c_str()));

    std::vector<std::string> warnings;
    bool no_models_on_plate = false;
    if (!m_last_context_snapshot_json.empty()) {
        try {
            const nlohmann::json context_json = nlohmann::json::parse(m_last_context_snapshot_json);
            if (context_json.contains("project") && context_json.at("project").is_object()) {
                const nlohmann::json& project = context_json.at("project");
                if (project.contains("object_count") && project.at("object_count").is_number_integer())
                    no_models_on_plate = project.at("object_count").get<int>() <= 0;
            }
        } catch (...) {
            // Keep context card resilient to malformed cached JSON.
        }
    }
    if (no_models_on_plate)
        warnings.emplace_back("No models on the plate. Add a model to see the magic.");
    if (printer_missing)
        warnings.emplace_back("Printer preset is missing.");
    if (filament_missing)
        warnings.emplace_back("Filament preset is missing.");

    if (warnings.empty()) {
        m_context_warning->Show(false);
        m_context_warning->SetLabel("");
    } else {
        std::ostringstream warning_lines;
        for (size_t i = 0; i < warnings.size(); ++i) {
            if (i > 0)
                warning_lines << "\n";
            warning_lines << warnings[i];
        }
        m_context_warning->SetLabel(wxString::FromUTF8(warning_lines.str().c_str()));
        m_context_warning->Show(true);
    }

    const bool has_session_state = !m_last_context_snapshot_json.empty() ||
                                   !m_last_geometry_insights_json.empty() ||
                                   !m_last_ai_response_json.empty() ||
                                   !m_last_validation_errors.empty() ||
                                   !m_recommended_changes.empty() ||
                                   (m_history != nullptr && !m_history->IsEmpty());
    if (m_change_printer != nullptr)
        m_change_printer->Show(printer_missing);
    if (m_change_filament != nullptr)
        m_change_filament->Show(filament_missing);
    if (m_start_over != nullptr)
        m_start_over->Show(has_session_state);

    m_context_card->Layout();
    Layout();
}

void AISliceAssistantPanel::clear_recommendations()
{
    m_recommended_changes.clear();
    if (m_recommended_changes_list)
        m_recommended_changes_list->DeleteAllItems();
    if (m_change_details)
        m_change_details->Clear();
    refresh_context_card();
}

void AISliceAssistantPanel::populate_recommendations_from_response(const nlohmann::json& response_json)
{
    clear_recommendations();

    if (!response_json.contains("recommended_changes") || !response_json.at("recommended_changes").is_array())
        return;

    const bool safe_mode = is_safe_mode_enabled();

    for (const auto& item : response_json.at("recommended_changes")) {
        if (safe_mode && m_recommended_changes.size() >= 10)
            break;
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

        if (!Slic3r::AI::Apply::AllowlistRegistry::is_allowed(change.key)) {
            change.blocked = true;
            change.blocked_reason = "not allowlisted";
        } else {
            if (safe_mode && Slic3r::AI::Apply::AllowlistRegistry::has_tag(change.key, "high-risk")) {
                change.blocked = true;
                change.blocked_reason = "blocked by safe mode (high-risk key)";
            }

            if (safe_mode && Slic3r::AI::Apply::AllowlistRegistry::has_any_tag(change.key, {"temperature", "flow", "speed"}))
                change.requires_user_confirmation = true;

            const auto value_validation = Slic3r::AI::Apply::AllowlistRegistry::validate_value(change.key, change.value);
            if (!value_validation.valid) {
                change.blocked = true;
                change.blocked_reason = value_validation.error_message.empty() ? "invalid value" : value_validation.error_message;
            }
        }

        m_recommended_changes.push_back(std::move(change));
    }

    for (size_t i = 0; i < m_recommended_changes.size(); ++i) {
        const auto& change = m_recommended_changes[i];
        const std::string label = Slic3r::AI::Apply::AllowlistRegistry::label_for(change.key);
        const std::string setting = label.empty() ? change.key : (label + " [" + change.key + "]");
        const std::string value = compact_json_value(change.value);
        std::string status = "OK";
        if (change.blocked) {
            status = "BLOCKED";
            if (!change.blocked_reason.empty())
                status += ": " + compact_status_reason(change.blocked_reason);
        }

        wxVector<wxVariant> row;
        row.push_back(wxVariant(!change.blocked));
        row.push_back(wxVariant(wxString::FromUTF8(setting.c_str())));
        row.push_back(wxVariant(wxString::FromUTF8(value.c_str())));
        row.push_back(wxVariant(wxString::FromUTF8(status.c_str())));
        m_recommended_changes_list->AppendItem(row);
    }

    if (!m_recommended_changes.empty()) {
        m_recommended_changes_list->SelectRow(0);
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
    details << "Blocked: " << (change.blocked ? "yes" : "no") << "\\n";
    if (change.blocked && !change.blocked_reason.empty())
        details << "Blocked reason: " << change.blocked_reason << "\\n";
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

bool AISliceAssistantPanel::apply_selected_changes_atomically(size_t& applied_count, std::vector<std::string>& error_list)
{
    applied_count = 0;
    error_list.clear();

    if (m_plater == nullptr) {
        error_list.emplace_back("plater unavailable");
        return false;
    }

    if (m_recommended_changes.empty()) {
        error_list.emplace_back("no recommendations to apply");
        return false;
    }

    std::vector<PreparedOperation> prepared;
    prepared.reserve(m_recommended_changes.size());

    auto* bundle = wxGetApp().preset_bundle;
    if (bundle == nullptr) {
        error_list.emplace_back("preset bundle unavailable");
        return false;
    }

    const int selected_object_idx = m_plater->get_selected_object_idx();
    bool has_checked_changes = false;

    for (size_t i = 0; i < m_recommended_changes.size(); ++i) {
        wxVariant enabled_variant;
        m_recommended_changes_list->GetValue(enabled_variant, static_cast<unsigned int>(i), 0);
        if (!enabled_variant.GetBool())
            continue;
        has_checked_changes = true;

        const RecommendedChange& change = m_recommended_changes[i];

        if (!Slic3r::AI::Apply::AllowlistRegistry::is_allowed(change.key)) {
            error_list.emplace_back("key not allowed: " + change.key);
            continue;
        }
        const auto validation = Slic3r::AI::Apply::AllowlistRegistry::validate_value(change.key, change.value);
        if (!validation.valid) {
            std::string msg = "invalid value for key: " + change.key;
            if (!validation.error_message.empty())
                msg += " (" + validation.error_message + ")";
            error_list.push_back(std::move(msg));
            continue;
        }

        ApplyScope scope;
        if (!scope_from_string(change.applies_to, scope)) {
            error_list.emplace_back("unknown scope: " + change.applies_to + " for key: " + change.key);
            continue;
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
                error_list.emplace_back("object scope requested but no object is selected for key: " + change.key);
                continue;
            }
            ModelObject* object = m_plater->model().objects[static_cast<size_t>(object_idx)];
            if (object == nullptr) {
                error_list.emplace_back("selected object not available for key: " + change.key);
                continue;
            }
            target_config = static_cast<ConfigBase*>(const_cast<DynamicPrintConfig*>(&object->config.get()));
        }

        if (target_config == nullptr || !target_config->has(change.key)) {
            error_list.emplace_back("target does not support key: " + change.key);
            continue;
        }

        std::string serialized_new;
        if (!serialize_value_for_setting(change.value, change.value_type, serialized_new)) {
            error_list.emplace_back("cannot serialize value for key: " + change.key);
            continue;
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

    if (!has_checked_changes) {
        error_list.emplace_back("no checked changes");
        return false;
    }
    if (!error_list.empty())
        return false;
    if (prepared.empty()) {
        error_list.emplace_back("no valid changes to apply");
        return false;
    }

    std::vector<AppliedValueBackup> backups;
    backups.reserve(prepared.size());
    for (const PreparedOperation& op : prepared) {
        const ConfigOption* original = op.target_config->option(op.key);
        if (original == nullptr) {
            error_list.emplace_back("cannot snapshot key: " + op.key);
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
            error_list.push_back(std::string("apply failed on key '") + op.key + "': " + ex.what());
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
    applied_count = prepared.size();
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
