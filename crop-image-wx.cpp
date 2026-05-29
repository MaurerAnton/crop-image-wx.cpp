// crop-image-wx.cpp — GIMP-style wxWidgets image cropping tool
// Build: mkdir build && cd build && CC=clang CXX=clang++ cmake .. && cmake --build . -j$(nproc)

#include <wx/wx.h>
#include <wx/filedlg.h>
#include <wx/image.h>
#include <wx/filename.h>
#include <algorithm>
#include <cmath>

constexpr int HANDLE_SIZE = 8;
constexpr int HANDLE_HALF = HANDLE_SIZE / 2;
constexpr int HANDLE_HIT  = 6;
constexpr int MIN_CROP_PX = 10;

enum class HandleID {
    None, NorthWest, North, NorthEast, West, Center, East, SouthWest, South, SouthEast
};

class ImagePanel;
class MainFrame;

class ImagePanel : public wxPanel {
public:
    ImagePanel(wxWindow* parent);
    bool LoadImage(const wxString& path);
    bool HasImage() const { return m_img.IsOk(); }
    void ApplyCrop();
    void ResetCrop();
    wxImage GetCropped() const;

private:
    void OnPaint(wxPaintEvent&);
    void OnSize(wxSizeEvent&);
    void OnMouse(wxMouseEvent&);
    void OnKeyDown(wxKeyEvent&);

    void   Recalc();
    wxPoint S2I(const wxPoint& p) const;
    wxPoint I2S(const wxPoint& p) const;
    wxRect  I2S(const wxRect& r) const;
    HandleID HitTest(const wxPoint& pt) const;
    void    Clamp();
    void    DrawDim(wxDC& dc);
    void    DrawHnd(wxDC& dc);
    void    Cursor(const wxPoint& pt);

    wxImage  m_img;
    wxBitmap m_bmp;
    wxRect   m_irect;
    bool     m_cropEn    = false;
    wxRect   m_crop;
    bool     m_hasCrop   = false;
    bool     m_drag      = false;
    bool     m_create    = false;
    wxPoint  m_start;
    wxRect   m_orig;
    HandleID m_handle   = HandleID::None;
};

class MainFrame : public wxFrame {
public:
    MainFrame();
private:
    void OnOpen(wxCommandEvent&);
    void OnSave(wxCommandEvent&);
    void OnSaveAs(wxCommandEvent&);
    void OnCrop(wxCommandEvent&);
    void OnReset(wxCommandEvent&);
    ImagePanel* pnl = nullptr;
    wxString path, name;
};

class CropApp : public wxApp {
    bool OnInit() override {
        wxInitAllImageHandlers();
        (new MainFrame())->Show(true);
        return true;
    }
};
wxIMPLEMENT_APP(CropApp);

// ══════════════════════ MainFrame ══════════════════════

MainFrame::MainFrame()
    : wxFrame(nullptr, wxID_ANY, "Crop Tool — Untitled", wxDefaultPosition, wxSize(1024, 768))
{
    auto* fm = new wxMenu;
    fm->Append(wxID_OPEN,   "&Open...\tCtrl+O");
    fm->Append(wxID_SAVE,   "&Save\tCtrl+S");
    fm->Append(wxID_SAVEAS, "Save &As...\tCtrl+Shift+S");
    fm->AppendSeparator();
    fm->Append(wxID_CLOSE,  "E&xit\tCtrl+Q");
    auto* em = new wxMenu;
    em->Append(wxID_CUT,   "&Crop\tCtrl+Return");
    em->Append(wxID_UNDO,  "&Reset Crop\tEscape");
    auto* mb = new wxMenuBar;
    mb->Append(fm, "&File");
    mb->Append(em, "&Edit");
    SetMenuBar(mb);

    pnl = new ImagePanel(this);

    Bind(wxEVT_MENU, &MainFrame::OnOpen,   this, wxID_OPEN);
    Bind(wxEVT_MENU, &MainFrame::OnSave,   this, wxID_SAVE);
    Bind(wxEVT_MENU, &MainFrame::OnSaveAs, this, wxID_SAVEAS);
    Bind(wxEVT_MENU, &MainFrame::OnCrop,   this, wxID_CUT);
    Bind(wxEVT_MENU, &MainFrame::OnReset,  this, wxID_UNDO);
    Bind(wxEVT_MENU, [this](wxCommandEvent&){ Close(true); }, wxID_CLOSE);
}

void MainFrame::OnOpen(wxCommandEvent&) {
    wxFileDialog dlg(this, "Open", "", "",
        "Images|*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tiff|All|*.*",
        wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dlg.ShowModal() == wxID_CANCEL) return;
    wxString p = dlg.GetPath();
    if (pnl->LoadImage(p)) {
        path = p; name = dlg.GetFilename();
        SetTitle(name + " — Crop Tool");
    } else
        wxMessageBox("Failed to load image.", "Error", wxOK | wxICON_ERROR);
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
        "PNG|*.png|JPEG|*.jpg|BMP|*.bmp",
        wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (dlg.ShowModal() == wxID_CANCEL) return;
    wxString p = dlg.GetPath();
    if (img.SaveFile(p)) { path = p; name = wxFileName(p).GetFullName(); SetTitle(name + " — Crop Tool"); }
}

void MainFrame::OnCrop(wxCommandEvent&) {
    pnl->ApplyCrop();
    SetTitle((name.empty() ? "Untitled" : name) + " — Crop Tool");
}

void MainFrame::OnReset(wxCommandEvent&) { pnl->ResetCrop(); }

// ══════════════════════ ImagePanel ══════════════════════

ImagePanel::ImagePanel(wxWindow* parent)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize)
{
    SetBackgroundColour(wxColour(40, 40, 40));
    Bind(wxEVT_PAINT, &ImagePanel::OnPaint, this);
    Bind(wxEVT_SIZE,  &ImagePanel::OnSize,  this);
    Bind(wxEVT_MOTION,    &ImagePanel::OnMouse, this);
    Bind(wxEVT_LEFT_DOWN, &ImagePanel::OnMouse, this);
    Bind(wxEVT_LEFT_UP,   &ImagePanel::OnMouse, this);
    Bind(wxEVT_MOUSE_CAPTURE_LOST, [](wxMouseCaptureLostEvent&){});
    Bind(wxEVT_KEY_DOWN, &ImagePanel::OnKeyDown, this);
}

bool ImagePanel::LoadImage(const wxString& path) {
    wxImage img(path);
    if (!img.IsOk()) return false;
    m_img = img;
    m_hasCrop = false;
    m_crop = wxRect(0, 0, img.GetWidth(), img.GetHeight());
    m_cropEn = true;
    Layout();
    m_bmp = wxBitmap(m_img.Scale(m_irect.width, m_irect.height, wxIMAGE_QUALITY_BILINEAR));
    Refresh();
    return true;
}

void ImagePanel::ApplyCrop() {
    if (!m_img.IsOk() || !m_hasCrop) return;
    wxRect r = m_crop.Intersect(wxRect(0, 0, m_img.GetWidth(), m_img.GetHeight()));
    if (r.width < 1 || r.height < 1) return;
    m_img = m_img.GetSubImage(r);
    m_hasCrop = false;
    m_crop = wxRect(0, 0, m_img.GetWidth(), m_img.GetHeight());
    Layout();
    m_bmp = wxBitmap(m_img.Scale(m_irect.width, m_irect.height, wxIMAGE_QUALITY_BILINEAR));
    Refresh();
}

void ImagePanel::ResetCrop() {
    if (!m_img.IsOk()) return;
    m_hasCrop = false;
    m_crop = wxRect(0, 0, m_img.GetWidth(), m_img.GetHeight());
    Refresh();
}

wxImage ImagePanel::GetCropped() const {
    if (!m_img.IsOk()) return m_img;
    if (!m_hasCrop) return m_img;
    wxRect r = m_crop.Intersect(wxRect(0, 0, m_img.GetWidth(), m_img.GetHeight()));
    return (r.width > 0 && r.height > 0) ? m_img.GetSubImage(r) : m_img;
}

void ImagePanel::Recalc() {
    if (!m_img.IsOk()) return;
    int cw, ch;
    GetClientSize(&cw, &ch);
    if (cw < 1 || ch < 1) return;
    int iw = m_img.GetWidth(), ih = m_img.GetHeight();
    double s = std::min(double(cw)/iw, double(ch)/ih);
    m_irect.width  = std::max(1, int(iw * s));
    m_irect.height = std::max(1, int(ih * s));
    m_irect.x = (cw - m_irect.width)  / 2;
    m_irect.y = (ch - m_irect.height) / 2;
}

wxPoint ImagePanel::S2I(const wxPoint& p) const {
    if (m_irect.width < 1 || m_irect.height < 1) return {0,0};
    return {int((p.x - m_irect.x) * m_img.GetWidth()  / double(m_irect.width)),
            int((p.y - m_irect.y) * m_img.GetHeight() / double(m_irect.height))};
}
wxPoint ImagePanel::I2S(const wxPoint& p) const {
    if (m_irect.width < 1 || m_irect.height < 1) return {0,0};
    return {int(m_irect.x + p.x * double(m_irect.width)  / m_img.GetWidth()),
            int(m_irect.y + p.y * double(m_irect.height) / m_img.GetHeight())};
}
wxRect ImagePanel::I2S(const wxRect& r) const {
    return wxRect(I2S(r.GetTopLeft()), I2S(r.GetBottomRight()));
}

HandleID ImagePanel::HitTest(const wxPoint& pt) const {
    if (!m_hasCrop) return HandleID::None;
    wxRect r = I2S(m_crop);
    int hit = HANDLE_HALF + HANDLE_HIT;
    struct { int x, y; HandleID id; } pts[] = {
        {r.x,              r.y,               HandleID::NorthWest},
        {r.x+r.width/2,   r.y,               HandleID::North},
        {r.x+r.width,     r.y,               HandleID::NorthEast},
        {r.x,              r.y+r.height/2,   HandleID::West},
        {r.x+r.width,     r.y+r.height/2,   HandleID::East},
        {r.x,              r.y+r.height,     HandleID::SouthWest},
        {r.x+r.width/2,   r.y+r.height,     HandleID::South},
        {r.x+r.width,     r.y+r.height,     HandleID::SouthEast},
    };
    for (auto& p : pts)
        if (abs(pt.x - p.x) <= hit && abs(pt.y - p.y) <= hit) return p.id;
    if (r.Contains(pt)) return HandleID::Center;
    return HandleID::None;
}

void ImagePanel::Clamp() {
    int iw = m_img.GetWidth(), ih = m_img.GetHeight();
    if (m_crop.width  < MIN_CROP_PX) m_crop.width  = MIN_CROP_PX;
    if (m_crop.height < MIN_CROP_PX) m_crop.height = MIN_CROP_PX;
    if (m_crop.x < 0) m_crop.x = 0;
    if (m_crop.y < 0) m_crop.y = 0;
    if (m_crop.GetRight()  > iw) m_crop.x = iw - m_crop.width;
    if (m_crop.GetBottom() > ih) m_crop.y = ih - m_crop.height;
}

void ImagePanel::Cursor(const wxPoint& pt) {
    switch (HitTest(pt)) {
    case HandleID::NorthWest: case HandleID::SouthEast:
        SetCursor(wxCursor(wxCURSOR_SIZENWSE)); break;
    case HandleID::NorthEast: case HandleID::SouthWest:
        SetCursor(wxCursor(wxCURSOR_SIZENESW)); break;
    case HandleID::North: case HandleID::South:
        SetCursor(wxCursor(wxCURSOR_SIZENS)); break;
    case HandleID::West: case HandleID::East:
        SetCursor(wxCursor(wxCURSOR_SIZEWE)); break;
    case HandleID::Center: SetCursor(wxCursor(wxCURSOR_SIZING)); break;
    default: SetCursor(wxCursor(wxCURSOR_CROSS)); break;
    }
}

void ImagePanel::OnMouse(wxMouseEvent& evt) {
    if (!m_img.IsOk() || !m_cropEn) { evt.Skip(); return; }
    wxPoint pt = evt.GetPosition();

    if (evt.LeftDown()) {
        SetFocus();
        HandleID hit = HitTest(pt);
        if (hit != HandleID::None) {
            m_handle = hit; m_drag = true; m_create = false;
            m_start = pt; m_orig = m_crop; CaptureMouse();
        } else {
            m_handle = HandleID::SouthEast; m_drag = true; m_create = true;
            m_start = pt;
            wxPoint ip = S2I(pt);
            m_crop = wxRect(ip.x, ip.y, 1, 1); m_hasCrop = true;
            CaptureMouse();
        }
        Refresh(); return;
    }

    if (evt.Dragging() && m_drag) {
        wxPoint d = S2I(pt) - S2I(m_start);
        if (m_create) {
            wxPoint a = S2I(m_start), b = S2I(pt);
            m_crop = wxRect(std::min(a.x,b.x), std::min(a.y,b.y), abs(b.x-a.x), abs(b.y-a.y));
        } else {
            auto& r = m_orig;
            switch (m_handle) {
            case HandleID::NorthWest: m_crop = wxRect(r.x+d.x,r.y+d.y,r.width-d.x,r.height-d.y); break;
            case HandleID::North:     m_crop = wxRect(r.x,r.y+d.y,r.width,r.height-d.y); break;
            case HandleID::NorthEast: m_crop = wxRect(r.x,r.y+d.y,r.width+d.x,r.height-d.y); break;
            case HandleID::West:      m_crop = wxRect(r.x+d.x,r.y,r.width-d.x,r.height); break;
            case HandleID::East:      m_crop = wxRect(r.x,r.y,r.width+d.x,r.height); break;
            case HandleID::SouthWest: m_crop = wxRect(r.x+d.x,r.y,r.width-d.x,r.height+d.y); break;
            case HandleID::South:     m_crop = wxRect(r.x,r.y,r.width,r.height+d.y); break;
            case HandleID::SouthEast: m_crop = wxRect(r.x,r.y,r.width+d.x,r.height+d.y); break;
            case HandleID::Center:    m_crop = wxRect(r.x+d.x,r.y+d.y,r.width,r.height); break;
            default: break;
            }
        }
        Clamp(); Refresh(); return;
    }

    if (evt.LeftUp() && m_drag) {
        m_drag = false; m_create = false; m_handle = HandleID::None;
        if (HasCapture()) ReleaseMouse();
        if (m_crop.width < MIN_CROP_PX || m_crop.height < MIN_CROP_PX) m_hasCrop = false;
        Refresh(); return;
    }

    if (evt.Moving()) Cursor(pt); else evt.Skip();
}

void ImagePanel::OnKeyDown(wxKeyEvent& evt) {
    if (!m_img.IsOk()) { evt.Skip(); return; }
    if (evt.GetKeyCode() == WXK_ESCAPE) ResetCrop();
    else if (evt.GetKeyCode() == WXK_RETURN && evt.ControlDown() && m_hasCrop) ApplyCrop();
    else evt.Skip();
}

void ImagePanel::OnPaint(wxPaintEvent&) {
    wxPaintDC dc(this);
    dc.SetBackground(wxBrush(GetBackgroundColour()));
    dc.Clear();

    if (!m_img.IsOk()) {
        dc.SetTextForeground(wxColour(150, 150, 150));
        dc.DrawText("Drag an image here or File → Open", 20, 20);
        return;
    }
    if (m_bmp.IsOk()) dc.DrawBitmap(m_bmp, m_irect.x, m_irect.y);
    if (m_hasCrop && m_cropEn) { DrawDim(dc); DrawHnd(dc); }
}

void ImagePanel::DrawDim(wxDC& dc) {
    wxRect cr = I2S(m_crop).Intersect(m_irect);
    if (cr.width < 1 || cr.height < 1) return;

    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(wxBrush(wxColour(0, 0, 0, 140)));

    int ib = m_irect.y + m_irect.height;
    int ir = m_irect.x + m_irect.width;
    if (cr.y > m_irect.y)
        dc.DrawRectangle(m_irect.x, m_irect.y, m_irect.width, cr.y - m_irect.y);
    if (cr.y + cr.height < ib)
        dc.DrawRectangle(m_irect.x, cr.y + cr.height, m_irect.width, ib - (cr.y + cr.height));
    if (cr.x > m_irect.x)
        dc.DrawRectangle(m_irect.x, cr.y, cr.x - m_irect.x, cr.height);
    if (cr.x + cr.width < ir)
        dc.DrawRectangle(cr.x + cr.width, cr.y, ir - (cr.x + cr.width), cr.height);

    dc.SetPen(wxPen(wxColour(255, 255, 255, 220), 1, wxPENSTYLE_SHORT_DASH));
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    dc.DrawRectangle(cr);

    dc.SetPen(wxPen(wxColour(255, 255, 255, 40), 1, wxPENSTYLE_DOT));
    for (int i = 1; i < 3; i++) {
        int gx = cr.x + cr.width  * i / 3;
        int gy = cr.y + cr.height * i / 3;
        dc.DrawLine(gx, cr.y, gx, cr.y + cr.height);
        dc.DrawLine(cr.x, gy, cr.x + cr.width, gy);
    }
}

void ImagePanel::DrawHnd(wxDC& dc) {
    wxRect r = I2S(m_crop);
    wxPoint pts[8] = {
        {r.x, r.y}, {r.x+r.width/2, r.y}, {r.x+r.width, r.y},
        {r.x, r.y+r.height/2}, {r.x+r.width, r.y+r.height/2},
        {r.x, r.y+r.height}, {r.x+r.width/2, r.y+r.height}, {r.x+r.width, r.y+r.height}
    };
    dc.SetPen(wxPen(wxColour(255,255,255), 1));
    dc.SetBrush(wxBrush(wxColour(80,80,80)));
    for (auto& p : pts)
        dc.DrawRectangle(p.x - HANDLE_HALF, p.y - HANDLE_HALF, HANDLE_SIZE, HANDLE_SIZE);
}

void ImagePanel::OnSize(wxSizeEvent&) {
    if (!m_img.IsOk()) return;
    Layout();
    m_bmp = wxBitmap(m_img.Scale(m_irect.width, m_irect.height, wxIMAGE_QUALITY_BILINEAR));
    Refresh();
}
