#ifndef slic3r_GUI_AISliceAssistantPanel_hpp_
#define slic3r_GUI_AISliceAssistantPanel_hpp_

#include <string>
#include <vector>

#include "../../../ai/apply/allowlist_registry.h"
#include "../../../ai/providers/fake_provider.h"
#include "../../../ai/validation/response_validator.h"
#include "nlohmann/json.hpp"
#include <wx/panel.h>
#include <wx/window.h>

class wxButton;
class wxDataViewEvent;
class wxDataViewListCtrl;
class wxTextCtrl;
class wxCommandEvent;
class wxKeyEvent;
class wxShowEvent;
class wxSizeEvent;
class wxString;

namespace Slic3r {
namespace GUI {

class Plater;

class AISliceAssistantPanel : public wxPanel
{
public:
    explicit AISliceAssistantPanel(wxWindow* parent);

private:
    struct RecommendedChange
    {
        std::string id;
        std::string key;
        nlohmann::json value;
        std::string value_type;
        std::string reason;
        int quality { 0 };
        int time { 0 };
        int risk { 0 };
        double confidence { 0.0 };
        std::string applies_to;
        std::vector<std::string> tags;
        bool requires_user_confirmation { false };
        bool blocked { false };
        std::string blocked_reason;
    };

public:
    enum class ApplyScope
    {
        Global,
        Profile,
        Object
    };

    struct AppliedValueBackup
    {
        ApplyScope   scope { ApplyScope::Global };
        int          object_idx { -1 };
        std::string  key;
        std::string  serialized_value;
    };

private:
    void on_send(wxCommandEvent& event);
    void on_input_char_hook(wxKeyEvent& event);
    void on_panel_show(wxShowEvent& event);
    void on_panel_size(wxSizeEvent& event);
    void on_copy_context(wxCommandEvent& event);
    void on_copy_last_json(wxCommandEvent& event);
    void on_export_debug_bundle(wxCommandEvent& event);
    void on_apply(wxCommandEvent& event);
    void on_undo(wxCommandEvent& event);
    void on_change_list_event(wxDataViewEvent& event);

    void append_history_line(const wxString& line);
    void clear_recommendations();
    void populate_recommendations_from_response(const nlohmann::json& response_json);
    void update_change_details(int index);
    bool apply_selected_changes_atomically(size_t& applied_count, std::vector<std::string>& error_list);
    bool undo_last_apply_atomically(std::string& error_message);
    void refresh_plater_after_changes(bool touched_global_or_profile, bool touched_object, int object_idx);

    Plater*     m_plater { nullptr };
    wxTextCtrl* m_history { nullptr };
    wxDataViewListCtrl* m_recommended_changes_list { nullptr };
    wxTextCtrl* m_change_details { nullptr };
    wxTextCtrl* m_input   { nullptr };
    wxButton*   m_send    { nullptr };
    wxButton*   m_copy_context { nullptr };
    wxButton*   m_copy_last_json { nullptr };
    wxButton*   m_export_debug { nullptr };
    wxButton*   m_apply { nullptr };
    wxButton*   m_undo { nullptr };
    std::string m_last_context_snapshot_json;
    std::string m_last_geometry_insights_json;
    std::string m_last_ai_response_json;
    std::vector<std::string> m_last_validation_errors;
    std::vector<RecommendedChange> m_recommended_changes;
    std::vector<AppliedValueBackup> m_last_apply_backups;
    bool m_last_apply_touched_global_or_profile { false };
    bool m_last_apply_touched_object { false };
    int  m_last_apply_object_idx { -1 };
    Slic3r::AI::Providers::FakeProvider m_fake_provider;
    Slic3r::AI::Validation::ResponseValidator m_response_validator;
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GUI_AISliceAssistantPanel_hpp_
