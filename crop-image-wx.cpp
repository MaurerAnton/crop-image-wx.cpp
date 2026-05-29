// crop-image-wx.cpp — GIMP-style wxWidgets image cropping tool
// Build: mkdir build && cd build && CC=clang CXX=clang++ cmake .. && cmake --build . -j$(nproc)

#include <wx/wx.h>
#include <wx/filedlg.h>
#include <wx/image.h>
#include <wx/dcbuffer.h>
#include <wx/filename.h>
#include <algorithm>
#include <cmath>

// ─── Constants ───────────────────────────────────────────────────────────────

constexpr int HANDLE_SIZE    = 8;
constexpr int HANDLE_HALF    = HANDLE_SIZE / 2;
constexpr int HANDLE_HIT     = 6;
constexpr int MIN_CROP_PX    = 10;

enum class HandleID {
    None,
    NorthWest, North, NorthEast,
    West, Center, East,
    SouthWest, South, SouthEast
};

// ─── Forward decls ───────────────────────────────────────────────────────────

class ImagePanel;
class MainFrame;

// ─── ImagePanel ──────────────────────────────────────────────────────────────

class ImagePanel : public wxPanel {
public:
    ImagePanel(wxWindow* parent);

    bool LoadImage(const wxString& path);
    bool HasImage() const { return m_original.IsOk(); }
    void ApplyCrop();
    void ResetCrop();
    wxImage GetCropped() const;

private:
    void OnPaint(wxPaintEvent&);
    void OnSize(wxSizeEvent&);
    void OnMouse(wxMouseEvent&);
    void OnKeyDown(wxKeyEvent&);

    void RecalcLayout();
    void RenderBuffer();

    wxPoint    ScreenToImage(const wxPoint& screenPt) const;
    wxPoint    ImageToScreen(const wxPoint& imagePt) const;
    wxRect     ImageToScreen(const wxRect& imageRect) const;
    HandleID   HitTestHandle(const wxPoint& screenPt) const;
    void       ConstrainCropRect();
    void       DrawCropOverlay(wxDC& dc);
    void       DrawHandles(wxDC& dc);
    void       SetCursorForPos(const wxPoint& screenPt);

    wxImage     m_original;
    wxBitmap    m_bitmap;
    wxRect      m_imageRect;
    bool        m_cropEnabled    = false;
    wxRect      m_cropRect;
    bool        m_hasCropRect    = false;
    bool        m_dragging       = false;
    bool        m_creatingCrop   = false;
    wxPoint     m_dragStart;
    wxRect      m_dragOrigCrop;
    HandleID    m_activeHandle   = HandleID::None;
    wxCursor    m_curNWSW;
    wxCursor    m_curNESW;
};

// ─── MainFrame ───────────────────────────────────────────────────────────────

class MainFrame : public wxFrame {
public:
    MainFrame();
private:
    void OnOpen(wxCommandEvent&);
    void OnSave(wxCommandEvent&);
    void OnSaveAs(wxCommandEvent&);
    void OnSaveAsSentinel();
    void OnCrop(wxCommandEvent&);
    void OnResetCrop(wxCommandEvent&);
    void OnClose(wxCommandEvent&);
    void UpdateTitle();

    ImagePanel*   m_panel = nullptr;
    wxString      m_path;
    wxString      m_name;
};

// ─── App ─────────────────────────────────────────────────────────────────────

class CropApp : public wxApp {
public:
    bool OnInit() override {
        wxInitAllImageHandlers();
        auto* frame = new MainFrame();
        frame->Show(true);
        return true;
    }
};

wxIMPLEMENT_APP(CropApp);

// ═══════════════════════════════════════════════════════════════════════════════
//  MainFrame
// ═══════════════════════════════════════════════════════════════════════════════

MainFrame::MainFrame()
    : wxFrame(nullptr, wxID_ANY, "Crop Tool — Untitled",
              wxDefaultPosition, wxSize(1024, 768))
{
    // ── Menu ──────────────────────────────────────────────────────────────
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

    // ── Panel ─────────────────────────────────────────────────────────────
    m_panel = new ImagePanel(this);

    // ── Bindings ──────────────────────────────────────────────────────────
    Bind(wxEVT_MENU, &MainFrame::OnOpen,      this, wxID_OPEN);
    Bind(wxEVT_MENU, &MainFrame::OnSave,      this, wxID_SAVE);
    Bind(wxEVT_MENU, &MainFrame::OnSaveAs,    this, wxID_SAVEAS);
    Bind(wxEVT_MENU, &MainFrame::OnCrop,      this, wxID_CUT);
    Bind(wxEVT_MENU, &MainFrame::OnResetCrop, this, wxID_UNDO);
    Bind(wxEVT_MENU, &MainFrame::OnClose,     this, wxID_CLOSE);

    CreateStatusBar();
    SetStatusText("Ready — drag an image or File → Open");
}

void MainFrame::OnOpen(wxCommandEvent&) {
    wxFileDialog dlg(this, "Open Image", "", "",
        "Images|*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tiff|All files|*.*",
        wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dlg.ShowModal() == wxID_CANCEL) return;

    wxString path = dlg.GetPath();
    if (m_panel->LoadImage(path)) {
        m_path = path;
        m_name = dlg.GetFilename();
        UpdateTitle();
        wxImage img = m_panel->GetCropped();
        SetStatusText(wxString::Format("Loaded %dx%d — drag to crop", img.GetWidth(), img.GetHeight()));
    } else {
        wxMessageBox("Failed to load image.", "Error", wxOK | wxICON_ERROR);
    }
}

void MainFrame::OnSave(wxCommandEvent&) {
    if (m_path.empty()) { CallAfter([this](){ OnSaveAsSentinel(); }); return; }
    wxImage img = m_panel->GetCropped();
    if (!img.IsOk()) return;
    if (img.SaveFile(m_path))
        SetStatusText("Saved.");
    else
        wxMessageBox("Failed to save.", "Error", wxOK | wxICON_ERROR);
}

void MainFrame::OnSaveAsSentinel() { wxCommandEvent d; OnSaveAs(d); }

void MainFrame::OnSaveAs(wxCommandEvent&) {
    wxImage img = m_panel->GetCropped();
    if (!img.IsOk()) return;
    wxFileDialog dlg(this, "Save As", "", m_name,
        "PNG|*.png|JPEG|*.jpg|BMP|*.bmp",
        wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (dlg.ShowModal() == wxID_CANCEL) return;
    wxString path = dlg.GetPath();
    if (img.SaveFile(path)) {
        m_path = path;
        m_name = wxFileName(path).GetFullName();
        UpdateTitle();
        SetStatusText("Saved.");
    } else {
        wxMessageBox("Failed to save.", "Error", wxOK | wxICON_ERROR);
    }
}

void MainFrame::OnCrop(wxCommandEvent&) {
    m_panel->ApplyCrop();
    UpdateTitle();
    wxImage img = m_panel->GetCropped();
    SetStatusText(wxString::Format("Cropped to %dx%d", img.GetWidth(), img.GetHeight()));
}

void MainFrame::OnResetCrop(wxCommandEvent&) {
    m_panel->ResetCrop();
    SetStatusText("Crop reset.");
}

void MainFrame::OnClose(wxCommandEvent&) { Close(true); }

void MainFrame::UpdateTitle() {
    SetTitle((m_name.empty() ? "Untitled" : m_name) + " — Crop Tool");
}

// ═══════════════════════════════════════════════════════════════════════════════
//  ImagePanel
// ═══════════════════════════════════════════════════════════════════════════════

ImagePanel::ImagePanel(wxWindow* parent)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
              wxFULL_REPAINT_ON_RESIZE)
{
    SetBackgroundColour(wxColour(40, 40, 40));
    SetMinSize(wxSize(200, 150));

    m_curNWSW = wxCursor(wxCURSOR_SIZENWSE);
    m_curNESW = wxCursor(wxCURSOR_SIZENESW);

    Bind(wxEVT_PAINT,       &ImagePanel::OnPaint,  this);
    Bind(wxEVT_SIZE,        &ImagePanel::OnSize,   this);
    Bind(wxEVT_MOTION,      &ImagePanel::OnMouse,  this);
    Bind(wxEVT_LEFT_DOWN,   &ImagePanel::OnMouse,  this);
    Bind(wxEVT_LEFT_UP,     &ImagePanel::OnMouse,  this);
    Bind(wxEVT_MOUSE_CAPTURE_LOST, [](wxMouseCaptureLostEvent&){});
    Bind(wxEVT_KEY_DOWN,    &ImagePanel::OnKeyDown, this);
}

// ─── Load ──────────────────────────────────────────────────────────────────

bool ImagePanel::LoadImage(const wxString& path) {
    wxImage img(path);
    if (!img.IsOk()) return false;
    m_original = img;
    m_hasCropRect = false;
    m_cropRect = wxRect(0, 0, img.GetWidth(), img.GetHeight());
    m_cropEnabled = true;
    RecalcLayout();
    RenderBuffer();
    Refresh();
    return true;
}

// ─── Crop ──────────────────────────────────────────────────────────────────

void ImagePanel::ApplyCrop() {
    if (!m_original.IsOk() || !m_hasCropRect) return;
    wxRect r = m_cropRect.Intersect(
        wxRect(0, 0, m_original.GetWidth(), m_original.GetHeight()));
    if (r.width < 1 || r.height < 1) return;
    m_original = m_original.GetSubImage(r);
    m_hasCropRect = false;
    m_cropRect = wxRect(0, 0, m_original.GetWidth(), m_original.GetHeight());
    RecalcLayout();
    RenderBuffer();
    Refresh();
}

void ImagePanel::ResetCrop() {
    if (!m_original.IsOk()) return;
    m_hasCropRect = false;
    m_cropRect = wxRect(0, 0, m_original.GetWidth(), m_original.GetHeight());
    Refresh();
}

wxImage ImagePanel::GetCropped() const {
    if (!m_original.IsOk()) return m_original;
    if (!m_hasCropRect) return m_original;
    wxRect r = m_cropRect.Intersect(
        wxRect(0, 0, m_original.GetWidth(), m_original.GetHeight()));
    return (r.width > 0 && r.height > 0) ? m_original.GetSubImage(r) : m_original;
}

// ─── Layout ────────────────────────────────────────────────────────────────

void ImagePanel::RecalcLayout() {
    if (!m_original.IsOk()) return;
    int cw, ch;
    GetClientSize(&cw, &ch);
    if (cw < 1 || ch < 1) return;

    int iw = m_original.GetWidth();
    int ih = m_original.GetHeight();
    double s = std::min(double(cw)/iw, double(ch)/ih);

    m_imageRect.width  = std::max(1, int(iw * s));
    m_imageRect.height = std::max(1, int(ih * s));
    m_imageRect.x = (cw - m_imageRect.width)  / 2;
    m_imageRect.y = (ch - m_imageRect.height) / 2;
}

void ImagePanel::RenderBuffer() {
    if (!m_original.IsOk()) return;
    int w = m_imageRect.width;
    int h = m_imageRect.height;
    if (w < 1 || h < 1) return;
    m_bitmap = wxBitmap(m_original.Scale(w, h, wxIMAGE_QUALITY_BILINEAR));
}

// ─── Coords ────────────────────────────────────────────────────────────────

wxPoint ImagePanel::ScreenToImage(const wxPoint& p) const {
    if (m_imageRect.width < 1 || m_imageRect.height < 1) return {0,0};
    return {
        int((p.x - m_imageRect.x) * double(m_original.GetWidth())  / m_imageRect.width),
        int((p.y - m_imageRect.y) * double(m_original.GetHeight()) / m_imageRect.height)
    };
}

wxPoint ImagePanel::ImageToScreen(const wxPoint& p) const {
    if (m_imageRect.width < 1 || m_imageRect.height < 1) return {0,0};
    return {
        int(m_imageRect.x + p.x * double(m_imageRect.width)  / m_original.GetWidth()),
        int(m_imageRect.y + p.y * double(m_imageRect.height) / m_original.GetHeight())
    };
}

wxRect ImagePanel::ImageToScreen(const wxRect& r) const {
    wxPoint tl = ImageToScreen(r.GetTopLeft());
    wxPoint br = ImageToScreen(r.GetBottomRight());
    return wxRect(tl, br);
}

// ─── Hit test ──────────────────────────────────────────────────────────────

HandleID ImagePanel::HitTestHandle(const wxPoint& pt) const {
    if (!m_hasCropRect) return HandleID::None;
    wxRect scr = ImageToScreen(m_cropRect);
    int hit = HANDLE_HALF + HANDLE_HIT;

    struct { int x, y; HandleID id; } pts[] = {
        {scr.x,              scr.y,               HandleID::NorthWest},
        {scr.x + scr.width/2,scr.y,               HandleID::North},
        {scr.x + scr.width,  scr.y,               HandleID::NorthEast},
        {scr.x,              scr.y + scr.height/2,HandleID::West},
        {scr.x + scr.width,  scr.y + scr.height/2,HandleID::East},
        {scr.x,              scr.y + scr.height,  HandleID::SouthWest},
        {scr.x + scr.width/2,scr.y + scr.height,  HandleID::South},
        {scr.x + scr.width,  scr.y + scr.height,  HandleID::SouthEast},
    };
    for (auto& p : pts)
        if (std::abs(pt.x - p.x) <= hit && std::abs(pt.y - p.y) <= hit)
            return p.id;
    if (scr.Contains(pt)) return HandleID::Center;
    return HandleID::None;
}

void ImagePanel::ConstrainCropRect() {
    int iw = m_original.GetWidth(), ih = m_original.GetHeight();
    if (m_cropRect.width  < MIN_CROP_PX) m_cropRect.width  = MIN_CROP_PX;
    if (m_cropRect.height < MIN_CROP_PX) m_cropRect.height = MIN_CROP_PX;
    if (m_cropRect.x < 0) m_cropRect.x = 0;
    if (m_cropRect.y < 0) m_cropRect.y = 0;
    if (m_cropRect.GetRight()  > iw) m_cropRect.x = iw - m_cropRect.width;
    if (m_cropRect.GetBottom() > ih) m_cropRect.y = ih - m_cropRect.height;
}

// ─── Mouse ─────────────────────────────────────────────────────────────────

void ImagePanel::OnMouse(wxMouseEvent& evt) {
    if (!m_original.IsOk() || !m_cropEnabled) { evt.Skip(); return; }
    wxPoint pt = evt.GetPosition();

    if (evt.LeftDown()) {
        SetFocus();
        HandleID hit = HitTestHandle(pt);
        if (hit != HandleID::None) {
            m_activeHandle = hit;
            m_dragging = true;
            m_creatingCrop = false;
            m_dragStart = pt;
            m_dragOrigCrop = m_cropRect;
            CaptureMouse();
        } else {
            m_activeHandle = HandleID::SouthEast;
            m_dragging = true;
            m_creatingCrop = true;
            m_dragStart = pt;
            wxPoint ip = ScreenToImage(pt);
            m_cropRect = wxRect(ip.x, ip.y, 1, 1);
            m_hasCropRect = true;
            CaptureMouse();
        }
        Refresh();
        return;
    }

    if (evt.Dragging() && m_dragging) {
        wxPoint id = ScreenToImage(pt) - ScreenToImage(m_dragStart);
        if (m_creatingCrop) {
            wxPoint a = ScreenToImage(m_dragStart);
            wxPoint b = ScreenToImage(pt);
            m_cropRect = wxRect(
                std::min(a.x,b.x), std::min(a.y,b.y),
                std::abs(b.x-a.x), std::abs(b.y-a.y));
        } else {
            auto& r = m_dragOrigCrop;
            switch (m_activeHandle) {
            case HandleID::NorthWest:
                m_cropRect = wxRect(r.x+id.x, r.y+id.y, r.width-id.x, r.height-id.y); break;
            case HandleID::North:
                m_cropRect = wxRect(r.x, r.y+id.y, r.width, r.height-id.y); break;
            case HandleID::NorthEast:
                m_cropRect = wxRect(r.x, r.y+id.y, r.width+id.x, r.height-id.y); break;
            case HandleID::West:
                m_cropRect = wxRect(r.x+id.x, r.y, r.width-id.x, r.height); break;
            case HandleID::East:
                m_cropRect = wxRect(r.x, r.y, r.width+id.x, r.height); break;
            case HandleID::SouthWest:
                m_cropRect = wxRect(r.x+id.x, r.y, r.width-id.x, r.height+id.y); break;
            case HandleID::South:
                m_cropRect = wxRect(r.x, r.y, r.width, r.height+id.y); break;
            case HandleID::SouthEast:
                m_cropRect = wxRect(r.x, r.y, r.width+id.x, r.height+id.y); break;
            case HandleID::Center:
                m_cropRect = wxRect(r.x+id.x, r.y+id.y, r.width, r.height); break;
            default: break;
            }
        }
        ConstrainCropRect();
        Refresh();
        return;
    }

    if (evt.LeftUp() && m_dragging) {
        m_dragging = false;
        m_creatingCrop = false;
        m_activeHandle = HandleID::None;
        if (HasCapture()) ReleaseMouse();
        if (m_cropRect.width < MIN_CROP_PX || m_cropRect.height < MIN_CROP_PX)
            m_hasCropRect = false;
        Refresh();
        return;
    }

    if (evt.Moving()) SetCursorForPos(pt);
    else evt.Skip();
}

void ImagePanel::OnKeyDown(wxKeyEvent& evt) {
    if (!m_original.IsOk()) { evt.Skip(); return; }
    if (evt.GetKeyCode() == WXK_ESCAPE) ResetCrop();
    else if (evt.GetKeyCode() == WXK_RETURN && evt.ControlDown() && m_hasCropRect)
        ApplyCrop();
    else evt.Skip();
}

void ImagePanel::SetCursorForPos(const wxPoint& pt) {
    if (!m_cropEnabled) { SetCursor(wxCursor(wxCURSOR_ARROW)); return; }
    switch (HitTestHandle(pt)) {
    case HandleID::NorthWest: case HandleID::SouthEast: SetCursor(m_curNWSW); break;
    case HandleID::NorthEast: case HandleID::SouthWest: SetCursor(m_curNESW); break;
    case HandleID::North: case HandleID::South: SetCursor(wxCursor(wxCURSOR_SIZENS)); break;
    case HandleID::West:  case HandleID::East:  SetCursor(wxCursor(wxCURSOR_SIZEWE)); break;
    case HandleID::Center: SetCursor(wxCursor(wxCURSOR_SIZING)); break;
    default: SetCursor(wxCursor(wxCURSOR_CROSS)); break;
    }
}

// ─── Paint ─────────────────────────────────────────────────────────────────

void ImagePanel::OnPaint(wxPaintEvent&) {
    wxBufferedPaintDC dc(this);
    dc.SetBackground(wxBrush(GetBackgroundColour()));
    dc.Clear();

    if (!m_original.IsOk()) {
        dc.SetTextForeground(wxColour(150, 150, 150));
        dc.SetFont(wxFont(14, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
        dc.DrawText("Drag an image here or File → Open", 20, 20);
        return;
    }

    if (m_bitmap.IsOk())
        dc.DrawBitmap(m_bitmap, m_imageRect.x, m_imageRect.y);

    if (m_hasCropRect && m_cropEnabled) {
        DrawCropOverlay(dc);
        DrawHandles(dc);
    }
}

void ImagePanel::DrawCropOverlay(wxDC& dc) {
    wxRect scr = ImageToScreen(m_cropRect);
    scr = scr.Intersect(m_imageRect);
    if (scr.width < 1 || scr.height < 1) return;

    // Dim overlay
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(wxBrush(wxColour(0, 0, 0, 140)));
    // top
    if (scr.y > m_imageRect.y)
        dc.DrawRectangle(m_imageRect.x, m_imageRect.y, m_imageRect.width, scr.y - m_imageRect.y);
    // bottom
    int be = scr.y + scr.height;
    int ib = m_imageRect.y + m_imageRect.height;
    if (be < ib)
        dc.DrawRectangle(m_imageRect.x, be, m_imageRect.width, ib - be);
    // left
    if (scr.x > m_imageRect.x)
        dc.DrawRectangle(m_imageRect.x, scr.y, scr.x - m_imageRect.x, scr.height);
    // right
    int re = scr.x + scr.width;
    int ir = m_imageRect.x + m_imageRect.width;
    if (re < ir)
        dc.DrawRectangle(re, scr.y, ir - re, scr.height);

    // Marching ants border
    dc.SetPen(wxPen(wxColour(255, 255, 255, 220), 1, wxPENSTYLE_SHORT_DASH));
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    dc.DrawRectangle(scr);

    // Rule of thirds
    dc.SetPen(wxPen(wxColour(255, 255, 255, 45), 1, wxPENSTYLE_DOT));
    for (int i = 1; i < 3; i++) {
        int gx = scr.x + scr.width  * i / 3;
        int gy = scr.y + scr.height * i / 3;
        dc.DrawLine(gx, scr.y, gx, scr.y + scr.height);
        dc.DrawLine(scr.x, gy, scr.x + scr.width, gy);
    }
}

void ImagePanel::DrawHandles(wxDC& dc) {
    wxRect scr = ImageToScreen(m_cropRect);
    wxPoint pts[8] = {
        {scr.x,              scr.y},
        {scr.x+scr.width/2, scr.y},
        {scr.x+scr.width,   scr.y},
        {scr.x,              scr.y+scr.height/2},
        {scr.x+scr.width,   scr.y+scr.height/2},
        {scr.x,              scr.y+scr.height},
        {scr.x+scr.width/2, scr.y+scr.height},
        {scr.x+scr.width,   scr.y+scr.height},
    };
    dc.SetPen(wxPen(wxColour(255, 255, 255), 1));
    dc.SetBrush(wxBrush(wxColour(80, 80, 80)));
    for (auto& p : pts)
        dc.DrawRectangle(p.x - HANDLE_HALF, p.y - HANDLE_HALF, HANDLE_SIZE, HANDLE_SIZE);
}

void ImagePanel::OnSize(wxSizeEvent&) {
    RecalcLayout();
    RenderBuffer();
    Refresh();
}
