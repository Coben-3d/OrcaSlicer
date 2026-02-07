#ifndef slic3r_GUI_AISliceAssistantPanel_hpp_
#define slic3r_GUI_AISliceAssistantPanel_hpp_

#include <wx/panel.h>

class wxButton;
class wxTextCtrl;
class wxCommandEvent;

namespace Slic3r {
namespace GUI {

class AISliceAssistantPanel : public wxPanel
{
public:
    explicit AISliceAssistantPanel(wxWindow* parent);

private:
    void on_send(wxCommandEvent& event);
    void append_history_line(const wxString& line);

    wxTextCtrl* m_history { nullptr };
    wxTextCtrl* m_input   { nullptr };
    wxButton*   m_send    { nullptr };
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GUI_AISliceAssistantPanel_hpp_
