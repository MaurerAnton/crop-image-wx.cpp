// crop-image-wx.cpp — GIMP-style wxWidgets image cropping tool
#include <wx/wx.h>
#include <wx/filedlg.h>
#include <wx/image.h>
#include <wx/filename.h>
#include <algorithm>
#include <cmath>

constexpr int H_SIZE = 8, H_HALF = 4, H_HIT = 6, MIN_CROP = 10;

enum class HID { None,NW,N,NE,W,C,E,SW,S,SE };

class ImagePanel : public wxPanel {
public:
    ImagePanel(wxWindow* p);
    bool Load(const wxString& path);
    bool HasImg() const { return m_img.IsOk(); }
    void Crop();
    void Reset();
    wxImage GetCropped() const;

private:
    void OnPaint(wxPaintEvent&);
    void OnSize(wxSizeEvent&);
    void OnMouse(wxMouseEvent&);
    void OnKeyDown(wxKeyEvent&);
    void Recalc();
    wxPoint S2I(const wxPoint& p) const;
    wxPoint I2S(const wxPoint& p) const;
    wxRect  I2S(const wxRect& r) const;
    HID     Hit(const wxPoint& pt) const;
    void    Clamp();
    void    DrawDim(wxDC& dc);
    void    DrawHnd(wxDC& dc);
    void    SetCur(const wxPoint& pt);

    wxImage m_img;
    wxBitmap m_bmp;
    wxRect  m_ir;
    bool    m_cropEn = false;
    wxRect  m_crop;
    bool    m_hasCrop = false;
    bool    m_drag = false, m_create = false;
    wxPoint m_start;
    wxRect  m_orig;
    HID     m_handle = HID::None;
};

class MainFrame : public wxFrame {
public:
    MainFrame();
    void PromptOpen();
    void LoadFile(const wxString& path);
private:
    void OnOpen(wxCommandEvent&);
    void OnSave(wxCommandEvent&);
    void OnSaveAs(wxCommandEvent&);
    void OnCrop(wxCommandEvent&);
    void OnReset(wxCommandEvent&);
    ImagePanel* pnl;
    wxString path, name;
};

class CropApp : public wxApp {
    bool OnInit() override {
        wxInitAllImageHandlers();
        auto* f = new MainFrame();
        f->Show(true);
        if (argc > 1) f->LoadFile(argv[1]);
        return true;
    }
};
wxIMPLEMENT_APP(CropApp);

MainFrame::MainFrame()
    : wxFrame(nullptr, wxID_ANY, "Crop Tool — Untitled", wxDefaultPosition, wxSize(1024, 768))
{
    pnl = new ImagePanel(this);

    // sizer so panel fills the frame
    auto* sz = new wxBoxSizer(wxVERTICAL);
    sz->Add(pnl, 1, wxEXPAND);
    SetSizer(sz);

    wxAcceleratorEntry ents[] = {
        {wxACCEL_CTRL, (int)'O', wxID_OPEN},
        {wxACCEL_CTRL, (int)'S', wxID_SAVE},
        {wxACCEL_CTRL | wxACCEL_SHIFT, (int)'S', wxID_SAVEAS},
        {wxACCEL_CTRL, WXK_RETURN, wxID_CUT},
    };
    SetAcceleratorTable(wxAcceleratorTable(4, ents));

    Bind(wxEVT_MENU, &MainFrame::OnOpen,   this, wxID_OPEN);
    Bind(wxEVT_MENU, &MainFrame::OnSave,   this, wxID_SAVE);
    Bind(wxEVT_MENU, &MainFrame::OnSaveAs, this, wxID_SAVEAS);
    Bind(wxEVT_MENU, &MainFrame::OnCrop,   this, wxID_CUT);
}

void MainFrame::PromptOpen() {
    wxCommandEvent dummy;
    OnOpen(dummy);
}

void MainFrame::LoadFile(const wxString& p) {
    if (pnl->Load(p)) {
        path = p; name = wxFileName(p).GetFullName();
        SetTitle(name + " — Crop Tool");
    }
}

void MainFrame::OnOpen(wxCommandEvent&) {
    wxFileDialog dlg(this, "Open", "", "",
        "Images|*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tiff|All|*.*",
        wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dlg.ShowModal() == wxID_CANCEL) return;
    LoadFile(dlg.GetPath());
}

void MainFrame::OnSave(wxCommandEvent&) {
    if (path.empty()) { wxCommandEvent d; OnSaveAs(d); return; }
    wxImage img = pnl->GetCropped();
    if (img.IsOk()) img.SaveFile(path);
}

void MainFrame::OnSaveAs(wxCommandEvent&) {
    wxImage img = pnl->GetCropped();
    if (!img.IsOk()) return;
    wxFileDialog dlg(this, "Save As", "", name,
        "PNG|*.png|JPEG|*.jpg|BMP|*.bmp", wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (dlg.ShowModal() == wxID_CANCEL) return;
    wxString p = dlg.GetPath();
    if (img.SaveFile(p)) { path = p; name = wxFileName(p).GetFullName(); SetTitle(name + " — Crop Tool"); }
}

void MainFrame::OnCrop(wxCommandEvent&) {
    pnl->Crop(); SetTitle((name.empty() ? "Untitled" : name) + " — Crop Tool");
}

void MainFrame::OnReset(wxCommandEvent&) { pnl->Reset(); }

ImagePanel::ImagePanel(wxWindow* p) : wxPanel(p) {
    SetBackgroundColour(wxColour(40,40,40));
    Bind(wxEVT_PAINT, &ImagePanel::OnPaint, this);
    Bind(wxEVT_SIZE,  &ImagePanel::OnSize,  this);
    Bind(wxEVT_MOTION,    &ImagePanel::OnMouse, this);
    Bind(wxEVT_LEFT_DOWN, &ImagePanel::OnMouse, this);
    Bind(wxEVT_LEFT_UP,   &ImagePanel::OnMouse, this);
    Bind(wxEVT_MOUSE_CAPTURE_LOST, [](wxMouseCaptureLostEvent&){});
    Bind(wxEVT_KEY_DOWN, &ImagePanel::OnKeyDown, this);
}

bool ImagePanel::Load(const wxString& path) {
    wxImage img(path);
    if (!img.IsOk()) return false;
    m_img = img;
    m_hasCrop = false;
    m_crop = wxRect(0,0,img.GetWidth(),img.GetHeight());
    m_cropEn = true;
    Recalc();
    if (m_ir.width < 1 || m_ir.height < 1) {
        // client size not available yet — defer to OnSize
        Refresh();
        return true;
    }
    m_bmp = wxBitmap(m_img.Scale(m_ir.width, m_ir.height, wxIMAGE_QUALITY_BILINEAR));
    Refresh();
    Update(); // force immediate repaint
    return true;
}

void ImagePanel::Crop() {
    if (!m_img.IsOk() || !m_hasCrop) return;
    wxRect r = m_crop.Intersect(wxRect(0,0,m_img.GetWidth(),m_img.GetHeight()));
    if (r.width < 1 || r.height < 1) return;
    m_img = m_img.GetSubImage(r);
    m_hasCrop = false;
    m_crop = wxRect(0,0,m_img.GetWidth(),m_img.GetHeight());
    Recalc();
    m_bmp = wxBitmap(m_img.Scale(m_ir.width, m_ir.height, wxIMAGE_QUALITY_BILINEAR));
    Refresh();
}

void ImagePanel::Reset() {
    if (!m_img.IsOk()) return;
    m_hasCrop = false;
    m_crop = wxRect(0,0,m_img.GetWidth(),m_img.GetHeight());
    Refresh();
}

wxImage ImagePanel::GetCropped() const {
    if (!m_img.IsOk()) return m_img;
    if (!m_hasCrop) return m_img;
    wxRect r = m_crop.Intersect(wxRect(0,0,m_img.GetWidth(),m_img.GetHeight()));
    return (r.width>0&&r.height>0) ? m_img.GetSubImage(r) : m_img;
}

void ImagePanel::Recalc() {
    if (!m_img.IsOk()) return;
    int cw, ch; GetClientSize(&cw, &ch);
    if (cw < 1 || ch < 1) return;
    int iw = m_img.GetWidth(), ih = m_img.GetHeight();
    double s = std::min(double(cw)/iw, double(ch)/ih);
    m_ir.width  = std::max(1, int(iw*s));
    m_ir.height = std::max(1, int(ih*s));
    m_ir.x = (cw - m_ir.width)/2;
    m_ir.y = (ch - m_ir.height)/2;
}

wxPoint ImagePanel::S2I(const wxPoint& p) const {
    if (m_ir.width<1||m_ir.height<1) return {0,0};
    return {int((p.x-m_ir.x)*m_img.GetWidth()/double(m_ir.width)),
            int((p.y-m_ir.y)*m_img.GetHeight()/double(m_ir.height))};
}
wxPoint ImagePanel::I2S(const wxPoint& p) const {
    if (m_ir.width<1||m_ir.height<1) return {0,0};
    return {int(m_ir.x+p.x*double(m_ir.width)/m_img.GetWidth()),
            int(m_ir.y+p.y*double(m_ir.height)/m_img.GetHeight())};
}
wxRect ImagePanel::I2S(const wxRect& r) const {
    return wxRect(I2S(r.GetTopLeft()), I2S(r.GetBottomRight()));
}

HID ImagePanel::Hit(const wxPoint& pt) const {
    if (!m_hasCrop) return HID::None;
    wxRect r = I2S(m_crop);
    int hit = H_HALF + H_HIT;
    struct { int x,y; HID id; } pts[] = {
        {r.x,            r.y,             HID::NW},{r.x+r.width/2, r.y,             HID::N},
        {r.x+r.width,   r.y,             HID::NE},{r.x,            r.y+r.height/2, HID::W},
        {r.x+r.width,   r.y+r.height/2, HID::E},{r.x,            r.y+r.height,   HID::SW},
        {r.x+r.width/2, r.y+r.height,   HID::S},{r.x+r.width,   r.y+r.height,   HID::SE},
    };
    for (auto& p : pts) if (abs(pt.x-p.x)<=hit && abs(pt.y-p.y)<=hit) return p.id;
    if (r.Contains(pt)) return HID::C;
    return HID::None;
}

void ImagePanel::Clamp() {
    if (m_crop.width < MIN_CROP) m_crop.width = MIN_CROP;
    if (m_crop.height < MIN_CROP) m_crop.height = MIN_CROP;
    m_crop = m_crop.Intersect(wxRect(0, 0, m_img.GetWidth(), m_img.GetHeight()));
    if (m_crop.width < MIN_CROP) m_crop.width = MIN_CROP;
    if (m_crop.height < MIN_CROP) m_crop.height = MIN_CROP;
}

void ImagePanel::SetCur(const wxPoint& pt) {
    switch (Hit(pt)) {
    case HID::NW: case HID::SE: SetCursor(wxCursor(wxCURSOR_SIZENWSE)); break;
    case HID::NE: case HID::SW: SetCursor(wxCursor(wxCURSOR_SIZENESW)); break;
    case HID::N: case HID::S: SetCursor(wxCursor(wxCURSOR_SIZENS)); break;
    case HID::W: case HID::E: SetCursor(wxCursor(wxCURSOR_SIZEWE)); break;
    case HID::C: SetCursor(wxCursor(wxCURSOR_SIZING)); break;
    default: SetCursor(wxCursor(wxCURSOR_CROSS)); break;
    }
}

void ImagePanel::OnMouse(wxMouseEvent& evt) {
    if (!m_img.IsOk() || !m_cropEn) {
        if (evt.LeftDown()) {
            // Use CallAfter to avoid nested event loop issues
            CallAfter([this](){
                wxFrame* f = dynamic_cast<wxFrame*>(wxGetTopLevelParent(this));
                if (auto* mf = dynamic_cast<MainFrame*>(f))
                    mf->PromptOpen();
            });
        }
        evt.Skip(); return;
    }
    wxPoint pt = evt.GetPosition();
    if (evt.LeftDown()) {
        SetFocus();
        HID hit = Hit(pt);
        if (hit != HID::None) {
            m_handle=hit; m_drag=true; m_create=false;
            m_start=pt; m_orig=m_crop; CaptureMouse();
        } else {
            m_handle=HID::SE; m_drag=true; m_create=true;
            m_start=pt;
            wxPoint ip=S2I(pt); m_crop=wxRect(ip.x,ip.y,1,1); m_hasCrop=true;
            Clamp();
            CaptureMouse();
        } Refresh(); return;
    }
    if (evt.Dragging() && m_drag) {
        wxPoint d=S2I(pt)-S2I(m_start);
        if (m_create) {
            wxPoint a=S2I(m_start), b=S2I(pt);
            m_crop=wxRect(std::min(a.x,b.x),std::min(a.y,b.y),abs(b.x-a.x),abs(b.y-a.y));
        } else {
            auto& r=m_orig;
            switch (m_handle) {
            case HID::NW: m_crop=wxRect(r.x+d.x,r.y+d.y,r.width-d.x,r.height-d.y); break;
            case HID::N:  m_crop=wxRect(r.x,r.y+d.y,r.width,r.height-d.y); break;
            case HID::NE: m_crop=wxRect(r.x,r.y+d.y,r.width+d.x,r.height-d.y); break;
            case HID::W:  m_crop=wxRect(r.x+d.x,r.y,r.width-d.x,r.height); break;
            case HID::E:  m_crop=wxRect(r.x,r.y,r.width+d.x,r.height); break;
            case HID::SW: m_crop=wxRect(r.x+d.x,r.y,r.width-d.x,r.height+d.y); break;
            case HID::S:  m_crop=wxRect(r.x,r.y,r.width,r.height+d.y); break;
            case HID::SE: m_crop=wxRect(r.x,r.y,r.width+d.x,r.height+d.y); break;
            case HID::C:  m_crop=wxRect(r.x+d.x,r.y+d.y,r.width,r.height); break;
            default: break;
            }
        } Clamp(); Refresh(); return;
    }
    if (evt.LeftUp() && m_drag) {
        m_drag=false; m_create=false; m_handle=HID::None;
        if (HasCapture()) ReleaseMouse();
        if (m_crop.width<MIN_CROP||m_crop.height<MIN_CROP) m_hasCrop=false;
        Refresh(); return;
    }
    if (evt.Moving()) SetCur(pt); else evt.Skip();
}

void ImagePanel::OnKeyDown(wxKeyEvent& evt) {
    if (!m_img.IsOk()) { evt.Skip(); return; }
    if (evt.GetKeyCode()==WXK_ESCAPE) Reset();
    else if (evt.GetKeyCode()==WXK_RETURN && evt.ControlDown() && m_hasCrop) Crop();
    else evt.Skip();
}

void ImagePanel::OnPaint(wxPaintEvent&) {
    wxPaintDC dc(this);
    dc.SetBackground(wxBrush(GetBackgroundColour()));
    dc.Clear();
    if (!m_img.IsOk()) {
        dc.SetTextForeground(wxColour(150,150,150));
        dc.DrawText("Click here or Ctrl+O to open an image", 20, 20);
        return;
    }
    if (m_bmp.IsOk()) dc.DrawBitmap(m_bmp, m_ir.x, m_ir.y);
    if (m_hasCrop && m_cropEn) { DrawDim(dc); DrawHnd(dc); }
}

void ImagePanel::DrawDim(wxDC& dc) {
    wxRect cr = I2S(m_crop).Intersect(m_ir);
    if (cr.width<1||cr.height<1) return;
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(wxBrush(wxColour(0,0,0,140)));
    int ib=m_ir.y+m_ir.height, ir=m_ir.x+m_ir.width;
    if (cr.y>m_ir.y) dc.DrawRectangle(m_ir.x,m_ir.y,m_ir.width,cr.y-m_ir.y);
    if (cr.y+cr.height<ib) dc.DrawRectangle(m_ir.x,cr.y+cr.height,m_ir.width,ib-(cr.y+cr.height));
    if (cr.x>m_ir.x) dc.DrawRectangle(m_ir.x,cr.y,cr.x-m_ir.x,cr.height);
    if (cr.x+cr.width<ir) dc.DrawRectangle(cr.x+cr.width,cr.y,ir-(cr.x+cr.width),cr.height);
    dc.SetPen(wxPen(wxColour(255,255,255,220),1,wxPENSTYLE_SHORT_DASH));
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    dc.DrawRectangle(cr);
    dc.SetPen(wxPen(wxColour(255,255,255,40),1,wxPENSTYLE_DOT));
    for (int i=1;i<3;i++) {
        int gx=cr.x+cr.width*i/3, gy=cr.y+cr.height*i/3;
        dc.DrawLine(gx,cr.y,gx,cr.y+cr.height);
        dc.DrawLine(cr.x,gy,cr.x+cr.width,gy);
    }
}

void ImagePanel::DrawHnd(wxDC& dc) {
    wxRect r=I2S(m_crop);
    wxPoint pts[8]={{r.x,r.y},{r.x+r.width/2,r.y},{r.x+r.width,r.y},
                    {r.x,r.y+r.height/2},{r.x+r.width,r.y+r.height/2},
                    {r.x,r.y+r.height},{r.x+r.width/2,r.y+r.height},{r.x+r.width,r.y+r.height}};
    dc.SetPen(wxPen(wxColour(255,255,255),1));
    dc.SetBrush(wxBrush(wxColour(80,80,80)));
    for (auto& p:pts) dc.DrawRectangle(p.x-H_HALF,p.y-H_HALF,H_SIZE,H_SIZE);
}

void ImagePanel::OnSize(wxSizeEvent&) {
    if (!m_img.IsOk()) return;
    Recalc();
    if (m_ir.width < 1 || m_ir.height < 1) return;
    m_bmp = wxBitmap(m_img.Scale(m_ir.width, m_ir.height, wxIMAGE_QUALITY_BILINEAR));
    Refresh();
}
