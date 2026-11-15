#include "ui/rounded_panel.h"

#include "ui/theme.h"

#include <wx/dcbuffer.h>
#include <wx/graphics.h>
#include <wx/sizer.h>

RoundedPanel::RoundedPanel(wxWindow* parent, int radius, int padding)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxFULL_REPAINT_ON_RESIZE),
      radius_(radius) {
    SetBackgroundColour(Theme::Surface());
    SetBackgroundStyle(wxBG_STYLE_PAINT);

    contentSizer_ = new wxBoxSizer(wxVERTICAL);

    auto* row = new wxBoxSizer(wxHORIZONTAL);
    row->AddSpacer(FromDIP(padding));
    row->Add(contentSizer_, 1, wxEXPAND);
    row->AddSpacer(FromDIP(padding));

    auto* root = new wxBoxSizer(wxVERTICAL);
    root->AddSpacer(FromDIP(padding));
    root->Add(row, 1, wxEXPAND);
    root->AddSpacer(FromDIP(padding));
    SetSizer(root);

    Bind(wxEVT_PAINT, &RoundedPanel::OnPaint, this);
    Bind(wxEVT_ERASE_BACKGROUND, [](wxEraseEvent&) {});
    Bind(wxEVT_SIZE, [this](wxSizeEvent& event) {
        // 拖动改变窗口大小时重绘自身即可，避免刷新所有子控件造成闪烁。
        Refresh();
        event.Skip();
    });
}

void RoundedPanel::AddContent(wxWindow* content, int proportion) {
    contentSizer_->Add(content, proportion, wxEXPAND);
}

void RoundedPanel::OnPaint(wxPaintEvent&) {
    wxAutoBufferedPaintDC dc(this);
    dc.SetBackground(wxBrush(Theme::Surface()));
    dc.Clear();

    wxRect rect = GetClientRect();
    if (rect.width <= 2 || rect.height <= 2) return;

    wxGraphicsContext* gc = wxGraphicsContext::Create(dc);
    if (!gc) return;
    gc->SetAntialiasMode(wxANTIALIAS_DEFAULT);

    if (horizontalBordersOnly_) {
        gc->SetPen(wxPen(Theme::Border(), 1));
        const double w = rect.width;
        const double h = rect.height;
        gc->StrokeLine(0, 0.5, w, 0.5);
        gc->StrokeLine(0, h - 0.5, w, h - 0.5);
        delete gc;
        return;
    }

    gc->SetBrush(*wxTRANSPARENT_BRUSH);
    gc->SetPen(wxPen(Theme::Border(), 1));
    rect.Deflate(1);
    gc->DrawRoundedRectangle(rect.x + 0.5, rect.y + 0.5, rect.width - 1, rect.height - 1,
                             FromDIP(radius_));
    delete gc;
}
