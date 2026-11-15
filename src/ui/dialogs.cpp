#include "ui/dialogs.h"

#include "ui/ui_text.h"

#include <wx/msgdlg.h>
#include <wx/window.h>

namespace dialogs {

int Message(wxWindow* parent, const wxString& message, const wxString& title,
            long style) {
    wxMessageDialog dialog(parent, message, title, style);
    if ((style & wxYES_NO) && (style & wxCANCEL)) {
        dialog.SetYesNoCancelLabels(UiText("dialogs.yes"), UiText("dialogs.no"),
                                    UiText("dialogs.cancel"));
    } else if (style & wxYES_NO) {
        dialog.SetYesNoLabels(UiText("dialogs.yes"), UiText("dialogs.no"));
    } else if (style & wxOK) {
        dialog.SetOKLabel(UiText("dialogs.ok"));
    }
    return dialog.ShowModal();
}

void LocalizeStandardButtons(wxWindow* dialog) {
    if (!dialog) return;
    if (wxWindow* ok = dialog->FindWindow(wxID_OK)) {
        ok->SetLabel(UiText("dialogs.ok"));
    }
    if (wxWindow* cancel = dialog->FindWindow(wxID_CANCEL)) {
        cancel->SetLabel(UiText("dialogs.cancel"));
    }
}

}  // namespace dialogs
