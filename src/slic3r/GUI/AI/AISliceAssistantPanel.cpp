#include "AISliceAssistantPanel.hpp"

#include "../../../ai/context_snapshot.h"
#include "../Plater.hpp"

#include <wx/button.h>
#include <wx/clipbrd.h>
#include <wx/dataobj.h>
#include <wx/sizer.h>
#include <wx/textctrl.h>

namespace Slic3r {
namespace GUI {

AISliceAssistantPanel::AISliceAssistantPanel(wxWindow* parent)
    : wxPanel(parent, wxID_ANY)
{
    m_plater = dynamic_cast<Plater*>(parent);

    auto* root_sizer = new wxBoxSizer(wxVERTICAL);

    m_history = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE | wxTE_READONLY);
    m_input   = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
    m_send    = new wxButton(this, wxID_ANY, "Send");
    m_copy_context = new wxButton(this, wxID_ANY, "Copy Context");

    auto* input_row = new wxBoxSizer(wxHORIZONTAL);
    input_row->Add(m_input, 1, wxEXPAND | wxRIGHT, FromDIP(6));
    input_row->Add(m_send, 0, wxRIGHT, FromDIP(6));
    input_row->Add(m_copy_context, 0, wxEXPAND);

    root_sizer->Add(m_history, 1, wxEXPAND | wxALL, FromDIP(8));
    root_sizer->Add(input_row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(8));
    SetSizer(root_sizer);

    m_send->Bind(wxEVT_BUTTON, &AISliceAssistantPanel::on_send, this);
    m_input->Bind(wxEVT_TEXT_ENTER, &AISliceAssistantPanel::on_send, this);
    m_copy_context->Bind(wxEVT_BUTTON, &AISliceAssistantPanel::on_copy_context, this);
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
