#ifndef slic3r_GUI_AISliceAssistantPanel_hpp_
#define slic3r_GUI_AISliceAssistantPanel_hpp_

#include <string>

#include "../../../ai/providers/fake_provider.h"
#include "../../../ai/validation/response_validator.h"
#include <wx/panel.h>

class wxButton;
class wxTextCtrl;
class wxCommandEvent;

namespace Slic3r {
namespace GUI {

class Plater;

class AISliceAssistantPanel : public wxPanel
{
public:
    explicit AISliceAssistantPanel(wxWindow* parent);

private:
    void on_send(wxCommandEvent& event);
    void on_copy_context(wxCommandEvent& event);
    void on_copy_last_json(wxCommandEvent& event);
    void append_history_line(const wxString& line);

    Plater*     m_plater { nullptr };
    wxTextCtrl* m_history { nullptr };
    wxTextCtrl* m_input   { nullptr };
    wxButton*   m_send    { nullptr };
    wxButton*   m_copy_context { nullptr };
    wxButton*   m_copy_last_json { nullptr };
    std::string m_last_context_snapshot_json;
    std::string m_last_geometry_insights_json;
    std::string m_last_ai_response_json;
    Slic3r::AI::Providers::FakeProvider m_fake_provider;
    Slic3r::AI::Validation::ResponseValidator m_response_validator;
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GUI_AISliceAssistantPanel_hpp_
