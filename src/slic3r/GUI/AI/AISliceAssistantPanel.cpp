#include "AISliceAssistantPanel.hpp"

#include <wx/button.h>
#include <wx/sizer.h>
#include <wx/textctrl.h>

namespace Slic3r {
namespace GUI {

AISliceAssistantPanel::AISliceAssistantPanel(wxWindow* parent)
    : wxPanel(parent, wxID_ANY)
{
    auto* root_sizer = new wxBoxSizer(wxVERTICAL);

    m_history = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE | wxTE_READONLY);
    m_input   = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
    m_send    = new wxButton(this, wxID_ANY, "Send");

    auto* input_row = new wxBoxSizer(wxHORIZONTAL);
    input_row->Add(m_input, 1, wxEXPAND | wxRIGHT, FromDIP(6));
    input_row->Add(m_send, 0, wxEXPAND);

    root_sizer->Add(m_history, 1, wxEXPAND | wxALL, FromDIP(8));
    root_sizer->Add(input_row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8));
    SetSizer(root_sizer);

    m_send->Bind(wxEVT_BUTTON, &AISliceAssistantPanel::on_send, this);
    m_input->Bind(wxEVT_TEXT_ENTER, &AISliceAssistantPanel::on_send, this);
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
    m_input->Clear();
}

void AISliceAssistantPanel::append_history_line(const wxString& line)
{
    if (m_history == nullptr)
        return;

    if (!m_history->IsEmpty())
        m_history->AppendText("\n");
    m_history->AppendText(line);
}

} // namespace GUI
} // namespace Slic3r
