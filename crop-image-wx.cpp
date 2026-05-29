// crop_tool.cpp — wxWidgets image cropping tool (GIMP-style)
// Build: g++ -std=c++20 crop_tool.cpp $(wx-config --cxxflags --libs) -o crop_tool
//        or use cmake

#include <wx/wx.h>
#include <wx/filedlg.h>
#include <wx/image.h>
#include <wx/dcbuffer.h>
#include <wx/filename.h>
#include <wx/dnd.h>
#include <algorithm>
#include <cmath>

// ─── Constants ───────────────────────────────────────────────────────────────

constexpr int HANDLE_SIZE    = 8;
constexpr int HANDLE_HALF    = HANDLE_SIZE / 2;
constexpr int HANDLE_HIT     = 6;  // extra hit tolerance, pixels
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

    void SetCropEnabled(bool enabled);
    void ApplyCrop();
    void ResetCrop();
    wxImage GetCropped() const;

private:
    void OnPaint(wxPaintEvent&);
    void OnSize(wxSizeEvent&);
    void OnMouse(wxMouseEvent&);
    void OnKeyDown(wxKeyEvent&);

    void RecalcLayout();
    void RenderToBuffer();

    // Coordinate conversion
    wxPoint    ScreenToImage(const wxPoint& screenPt) const;
    wxPoint    ImageToScreen(const wxPoint& imagePt) const;
    wxRect     ImageToScreen(const wxRect& imageRect) const;
    wxRect     ScreenToImage(const wxRect& screenRect) const;

    HandleID   HitTestHandle(const wxPoint& screenPt) const;
    void       ConstrainCropRect();
    void       DrawCropOverlay(wxDC& dc);
    void       DrawHandles(wxDC& dc);
    void       SetCursorForPos(const wxPoint& screenPt);

    // ── data ─────────────────────────────────────────────────────────────────
    wxImage     m_original;        // full-resolution original
    wxBitmap    m_displayBmp;      // scaled bitmap for painting
    wxRect      m_imageRect;       // where the image sits on screen (scaled)

    // crop state
    bool        m_cropEnabled    = false;
    wxRect      m_cropRect;         // in IMAGE coordinates
    bool        m_hasCropRect    = false;

    // interaction state
    bool        m_dragging       = false;
    bool        m_creatingCrop   = false;
    wxPoint     m_dragStart;        // screen coords
    wxRect      m_dragOrigCrop;     // image coords, snapshot at drag start
    HandleID    m_activeHandle   = HandleID::None;

    wxCursor    m_cursorNWSW;       // \ diagonal
    wxCursor    m_cursorNESW;       // / diagonal
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

    ImagePanel*   m_panel   = nullptr;
    wxString      m_currentPath;
    wxString      m_filenameTitle;   // "Untitled" or filename
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
//  MainFrame implementation
// ═══════════════════════════════════════════════════════════════════════════════

MainFrame::MainFrame()
    : wxFrame(nullptr, wxID_ANY, "Crop Tool — Untitled",
              wxDefaultPosition, wxSize(1024, 768))
{
    SetMinSize(wxSize(400, 300));

    // ── Icon (optional, skip if not available) ────────────────────────────
    // SetIcon(wxICON(appicon));

    // ── Menu bar ──────────────────────────────────────────────────────────
    auto* fileMenu = new wxMenu;
    fileMenu->Append(wxID_OPEN,  "&Open...\tCtrl+O");
    fileMenu->Append(wxID_SAVE,  "&Save\tCtrl+S");
    fileMenu->Append(wxID_SAVEAS,"Save &As...\tCtrl+Shift+S");
    fileMenu->AppendSeparator();
    fileMenu->Append(wxID_CLOSE, "E&xit\tCtrl+Q");

    auto* editMenu = new wxMenu;
    editMenu->Append(wxID_CUT,   "&Crop\tCtrl+Return");
    editMenu->Append(wxID_UNDO,  "&Reset Crop\tEscape");

    auto* menuBar = new wxMenuBar;
    menuBar->Append(fileMenu, "&File");
    menuBar->Append(editMenu, "&Edit");
    SetMenuBar(menuBar);

    // ── Status bar ────────────────────────────────────────────────────────
    CreateStatusBar(2);
    SetStatusText("Ready", 0);

    // ── Panel ─────────────────────────────────────────────────────────────
    m_panel = new ImagePanel(this);

    // ── Bindings ──────────────────────────────────────────────────────────
    Bind(wxEVT_MENU, &MainFrame::OnOpen,      this, wxID_OPEN);
    Bind(wxEVT_MENU, &MainFrame::OnSave,      this, wxID_SAVE);
    Bind(wxEVT_MENU, &MainFrame::OnSaveAs,    this, wxID_SAVEAS);
    Bind(wxEVT_MENU, &MainFrame::OnCrop,      this, wxID_CUT);
    Bind(wxEVT_MENU, &MainFrame::OnResetCrop, this, wxID_UNDO);
    Bind(wxEVT_MENU, &MainFrame::OnClose,     this, wxID_CLOSE);
}

void MainFrame::OnOpen(wxCommandEvent&) {
    wxFileDialog dlg(this, "Open Image", "",
                     "", "Images (*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tiff)|"
                         "*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tiff|"
                         "All files (*.*)|*.*",
                     wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dlg.ShowModal() == wxID_CANCEL) return;

    wxString path = dlg.GetPath();
    if (m_panel->LoadImage(path)) {
        m_currentPath = path;
        m_filenameTitle = dlg.GetFilename();
        UpdateTitle();
        wxSize imgSz(m_panel->GetCropped().GetSize());
        SetStatusText(wxString::Format("Loaded: %dx%d", imgSz.x, imgSz.y), 0);
    } else {
        wxMessageBox("Failed to load image.", "Error", wxOK | wxICON_ERROR);
    }
}

void MainFrame::OnSave(wxCommandEvent&) {
    if (m_currentPath.empty()) {
        CallAfter([this](){ OnSaveAsSentinel(); });
        return;
    }
    wxImage cropped = m_panel->GetCropped();
    if (!cropped.IsOk()) return;
    if (cropped.SaveFile(m_currentPath)) {
        SetStatusText("Saved.", 0);
    } else {
        wxMessageBox("Failed to save.", "Error", wxOK | wxICON_ERROR);
    }
}

void MainFrame::OnSaveAsSentinel() {
    wxCommandEvent dummy;
    OnSaveAs(dummy);
}

void MainFrame::OnSaveAs(wxCommandEvent&) {
    wxImage cropped = m_panel->GetCropped();
    if (!cropped.IsOk()) return;

    wxFileDialog dlg(this, "Save Image As", "",
                     m_filenameTitle,
                     "PNG (*.png)|*.png|JPEG (*.jpg)|*.jpg|BMP (*.bmp)|*.bmp",
                     wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (dlg.ShowModal() == wxID_CANCEL) return;

    wxString path = dlg.GetPath();
    if (cropped.SaveFile(path)) {
        m_currentPath = path;
        m_filenameTitle = wxFileName(path).GetFullName();
        UpdateTitle();
        SetStatusText("Saved.", 0);
    } else {
        wxMessageBox("Failed to save.", "Error", wxOK | wxICON_ERROR);
    }
}

void MainFrame::OnCrop(wxCommandEvent&) {
    bool hadCropRect = m_panel->HasImage();
    m_panel->ApplyCrop();
    UpdateTitle();
    wxImage img = m_panel->GetCropped();
    SetStatusText(wxString::Format("Cropped to %dx%d", img.GetWidth(), img.GetHeight()), 0);
}

void MainFrame::OnResetCrop(wxCommandEvent&) {
    m_panel->ResetCrop();
    SetStatusText("Crop reset.", 0);
}

void MainFrame::OnClose(wxCommandEvent&) { Close(true); }

void MainFrame::UpdateTitle() {
    wxString title = m_filenameTitle.empty() ? "Untitled" : m_filenameTitle;
    title += " — Crop Tool";
    SetTitle(title);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  ImagePanel implementation
// ═══════════════════════════════════════════════════════════════════════════════

ImagePanel::ImagePanel(wxWindow* parent)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
              wxFULL_REPAINT_ON_RESIZE)
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);   // double-buffering
    SetDoubleBuffered(true);
    SetMinSize(wxSize(200, 150));

    // Pre-build resize cursors. wxWidgets doesn't ship diagonal resize
    // cursors on all platforms, so we build ones from stock if needed.
    //
    // Strategy: use wxCursor(wxCURSOR_SIZENWSE) and wxCursor(wxCURSOR_SIZENESW)
    // which are available on GTK. On platforms without them we fall back to
    // sizing cursor.
#if defined(__WXGTK__) || defined(__WXMSW__) || defined(__WXMAC__)
    m_cursorNWSW = wxCursor(wxCURSOR_SIZENWSE);
    m_cursorNESW = wxCursor(wxCURSOR_SIZENESW);
#else
    m_cursorNWSW = wxCursor(wxCURSOR_SIZING);
    m_cursorNESW = wxCursor(wxCURSOR_SIZING);
#endif

    Bind(wxEVT_PAINT, &ImagePanel::OnPaint, this);
    Bind(wxEVT_SIZE,  &ImagePanel::OnSize,  this);
    Bind(wxEVT_MOTION,       &ImagePanel::OnMouse, this);
    Bind(wxEVT_LEFT_DOWN,     &ImagePanel::OnMouse, this);
    Bind(wxEVT_LEFT_UP,       &ImagePanel::OnMouse, this);
    Bind(wxEVT_MOUSE_CAPTURE_LOST, [this](wxMouseCaptureLostEvent&){});
    Bind(wxEVT_KEY_DOWN, &ImagePanel::OnKeyDown, this);
}

// ─── Image loading ────────────────────────────────────────────────────────

bool ImagePanel::LoadImage(const wxString& path) {
    wxImage img(path);
    if (!img.IsOk()) return false;

    m_original = img;
    m_cropRect = wxRect(0, 0, img.GetWidth(), img.GetHeight());
    m_hasCropRect = false;
    m_cropEnabled = true;
    RecalcLayout();
    RenderToBuffer();
    Refresh();
    return true;
}

// ─── Crop operations ──────────────────────────────────────────────────────

void ImagePanel::SetCropEnabled(bool enabled) {
    m_cropEnabled = enabled;
    Refresh();
}

void ImagePanel::ApplyCrop() {
    if (!m_original.IsOk()) return;
    if (!m_hasCropRect) return;

    wxRect r = m_cropRect;
    // Clamp to image bounds
    r = r.Intersect(wxRect(0, 0, m_original.GetWidth(), m_original.GetHeight()));
    if (r.width < 1 || r.height < 1) return;

    m_original = m_original.GetSubImage(r);
    m_hasCropRect = false;
    m_cropRect = wxRect(0, 0, m_original.GetWidth(), m_original.GetHeight());
    RecalcLayout();
    RenderToBuffer();
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
    wxRect r = m_cropRect;
    r = r.Intersect(wxRect(0, 0, m_original.GetWidth(), m_original.GetHeight()));
    if (r.width < 1 || r.height < 1) return m_original;
    return m_original.GetSubImage(r);
}

// ─── Layout ───────────────────────────────────────────────────────────────

void ImagePanel::RecalcLayout() {
    if (!m_original.IsOk()) return;

    int cw, ch;
    GetClientSize(&cw, &ch);
    if (cw <= 0 || ch <= 0) { cw = 100; ch = 100; }

    int iw = m_original.GetWidth();
    int ih = m_original.GetHeight();

    double scale = std::min(
        static_cast<double>(cw) / iw,
        static_cast<double>(ch) / ih);

    int dispW = static_cast<int>(iw * scale);
    int dispH = static_cast<int>(ih * scale);

    m_imageRect.x      = (cw - dispW) / 2;
    m_imageRect.y      = (ch - dispH) / 2;
    m_imageRect.width  = dispW;
    m_imageRect.height = dispH;
}

void ImagePanel::RenderToBuffer() {
    if (!m_original.IsOk()) return;
    if (m_imageRect.width <= 0 || m_imageRect.height <= 0) return;

    wxImage scaled = m_original.Scale(
        m_imageRect.width, m_imageRect.height, wxIMAGE_QUALITY_BILINEAR);
    m_displayBmp = wxBitmap(scaled);
}

// ─── Coordinate conversion ────────────────────────────────────────────────

wxPoint ImagePanel::ScreenToImage(const wxPoint& screenPt) const {
    double scaleX = static_cast<double>(m_original.GetWidth())  / m_imageRect.width;
    double scaleY = static_cast<double>(m_original.GetHeight()) / m_imageRect.height;
    return wxPoint(
        static_cast<int>((screenPt.x - m_imageRect.x) * scaleX),
        static_cast<int>((screenPt.y - m_imageRect.y) * scaleY));
}

wxPoint ImagePanel::ImageToScreen(const wxPoint& imagePt) const {
    double scaleX = static_cast<double>(m_imageRect.width)  / m_original.GetWidth();
    double scaleY = static_cast<double>(m_imageRect.height) / m_original.GetHeight();
    return wxPoint(
        static_cast<int>(m_imageRect.x + imagePt.x * scaleX),
        static_cast<int>(m_imageRect.y + imagePt.y * scaleY));
}

wxRect ImagePanel::ImageToScreen(const wxRect& imageRect) const {
    wxPoint tl = ImageToScreen(imageRect.GetTopLeft());
    wxPoint br = ImageToScreen(imageRect.GetBottomRight());
    return wxRect(tl, br);
}

wxRect ImagePanel::ScreenToImage(const wxRect& screenRect) const {
    wxPoint tl = ScreenToImage(screenRect.GetTopLeft());
    wxPoint br = ScreenToImage(screenRect.GetBottomRight());
    return wxRect(tl, br);
}

// ─── Handle hit-testing ───────────────────────────────────────────────────

HandleID ImagePanel::HitTestHandle(const wxPoint& screenPt) const {
    if (!m_hasCropRect) return HandleID::None;

    wxRect scr = ImageToScreen(m_cropRect);
    int x = screenPt.x, y = screenPt.y;
    int hit = HANDLE_HALF + HANDLE_HIT;

    const wxPoint corners[9] = {
        scr.GetTopLeft(),
        wxPoint(scr.x + scr.width/2, scr.y),
        scr.GetTopRight(),
        wxPoint(scr.x, scr.y + scr.height/2),
        scr.GetTopLeft(),  // Center — handled separately below
        wxPoint(scr.x + scr.width, scr.y + scr.height/2),
        scr.GetBottomLeft(),
        wxPoint(scr.x + scr.width/2, scr.y + scr.height),
        scr.GetBottomRight()
    };

    static const HandleID ids[9] = {
        HandleID::NorthWest, HandleID::North, HandleID::NorthEast,
        HandleID::West,      HandleID::Center,HandleID::East,
        HandleID::SouthWest, HandleID::South, HandleID::SouthEast
    };

    for (int i = 0; i < 9; i++) {
        if (i == 4) continue;  // center handled separately
        if (std::abs(x - corners[i].x) <= hit &&
            std::abs(y - corners[i].y) <= hit) {
            return ids[i];
        }
    }

    // Center: click inside the rect moves it
    if (scr.Contains(screenPt)) return HandleID::Center;

    return HandleID::None;
}

// ─── Constrain crop rect ──────────────────────────────────────────────────

void ImagePanel::ConstrainCropRect() {
    int iw = m_original.GetWidth();
    int ih = m_original.GetHeight();

    if (m_cropRect.width  < MIN_CROP_PX) m_cropRect.width  = MIN_CROP_PX;
    if (m_cropRect.height < MIN_CROP_PX) m_cropRect.height = MIN_CROP_PX;
    if (m_cropRect.x < 0)                m_cropRect.x = 0;
    if (m_cropRect.y < 0)                m_cropRect.y = 0;
    if (m_cropRect.x + m_cropRect.width  > iw) m_cropRect.x = iw - m_cropRect.width;
    if (m_cropRect.y + m_cropRect.height > ih) m_cropRect.y = ih - m_cropRect.height;
}

// ─── Mouse ────────────────────────────────────────────────────────────────

void ImagePanel::OnMouse(wxMouseEvent& evt) {
    if (!m_original.IsOk()) { evt.Skip(); return; }
    if (!m_cropEnabled)    { evt.Skip(); return; }

    wxPoint screenPt = evt.GetPosition();

    if (evt.LeftDown()) {
        SetFocus();  // receive keyboard events

        HandleID hit = HitTestHandle(screenPt);
        if (hit != HandleID::None) {
            // Resize or move existing crop rect
            m_activeHandle = hit;
            m_dragging = true;
            m_creatingCrop = false;
            m_dragStart = screenPt;
            m_dragOrigCrop = m_cropRect;
            CaptureMouse();
        } else {
            // Start creating a new crop rect
            m_activeHandle = HandleID::SouthEast;  // drag out from corner
            m_dragging = true;
            m_creatingCrop = true;
            m_dragStart = screenPt;

            wxPoint imgPt = ScreenToImage(screenPt);
            m_cropRect = wxRect(imgPt.x, imgPt.y, 1, 1);
            m_hasCropRect = true;
            CaptureMouse();
        }
        Refresh();
        return;
    }

    if (evt.Dragging() && m_dragging) {
        wxPoint delta = screenPt - m_dragStart;
        wxPoint imgDelta = ScreenToImage(screenPt) - ScreenToImage(m_dragStart);

        int iw = m_original.GetWidth();
        int ih = m_original.GetHeight();

        if (m_creatingCrop) {
            // Creating new rect from initial corner
            wxPoint startImg = ScreenToImage(m_dragStart);
            wxPoint endImg   = ScreenToImage(screenPt);
            int x = std::min(startImg.x, endImg.x);
            int y = std::min(startImg.y, endImg.y);
            int w = std::abs(endImg.x - startImg.x);
            int h = std::abs(endImg.y - startImg.y);
            m_cropRect = wxRect(x, y, w, h);
            ConstrainCropRect();

        } else {
            // Resize/move existing rect
            switch (m_activeHandle) {
            case HandleID::NorthWest:
                m_cropRect.x     = m_dragOrigCrop.x + imgDelta.x;
                m_cropRect.y     = m_dragOrigCrop.y + imgDelta.y;
                m_cropRect.width  = m_dragOrigCrop.width  - imgDelta.x;
                m_cropRect.height = m_dragOrigCrop.height - imgDelta.y;
                break;
            case HandleID::North:
                m_cropRect.y     = m_dragOrigCrop.y + imgDelta.y;
                m_cropRect.height = m_dragOrigCrop.height - imgDelta.y;
                break;
            case HandleID::NorthEast:
                m_cropRect.y     = m_dragOrigCrop.y + imgDelta.y;
                m_cropRect.width  = m_dragOrigCrop.width  + imgDelta.x;
                m_cropRect.height = m_dragOrigCrop.height - imgDelta.y;
                break;
            case HandleID::West:
                m_cropRect.x     = m_dragOrigCrop.x + imgDelta.x;
                m_cropRect.width  = m_dragOrigCrop.width  - imgDelta.x;
                break;
            case HandleID::East:
                m_cropRect.width  = m_dragOrigCrop.width  + imgDelta.x;
                break;
            case HandleID::SouthWest:
                m_cropRect.x     = m_dragOrigCrop.x + imgDelta.x;
                m_cropRect.width  = m_dragOrigCrop.width  - imgDelta.x;
                m_cropRect.height = m_dragOrigCrop.height + imgDelta.y;
                break;
            case HandleID::South:
                m_cropRect.height = m_dragOrigCrop.height + imgDelta.y;
                break;
            case HandleID::SouthEast:
                m_cropRect.width  = m_dragOrigCrop.width  + imgDelta.x;
                m_cropRect.height = m_dragOrigCrop.height + imgDelta.y;
                break;
            case HandleID::Center:
                m_cropRect.x = m_dragOrigCrop.x + imgDelta.x;
                m_cropRect.y = m_dragOrigCrop.y + imgDelta.y;
                break;
            default:
                break;
            }
            ConstrainCropRect();
        }
        Refresh();

        // Update status
        wxRect r = m_cropRect;
        wxFrame* frame = dynamic_cast<wxFrame*>(
            wxGetTopLevelParent(this));
        if (frame) {
            frame->SetStatusText(
                wxString::Format("Crop: %d, %d  %d×%d", r.x, r.y, r.width, r.height), 1);
        }
        return;
    }

    if (evt.LeftUp() && m_dragging) {
        m_dragging = false;
        m_creatingCrop = false;
        m_activeHandle = HandleID::None;
        if (HasCapture()) ReleaseMouse();

        // If crop rect is too small or essentially the whole image, discard it
        if (m_cropRect.width < MIN_CROP_PX || m_cropRect.height < MIN_CROP_PX) {
            m_hasCropRect = false;
        }
        Refresh();
        return;
    }

    // Motion — update cursor
    if (evt.Moving()) {
        SetCursorForPos(screenPt);
        return;
    }

    evt.Skip();
}

void ImagePanel::OnKeyDown(wxKeyEvent& evt) {
    if (!m_original.IsOk()) { evt.Skip(); return; }

    switch (evt.GetKeyCode()) {
    case WXK_ESCAPE:
        ResetCrop();
        break;
    case WXK_RETURN:
        if (evt.ControlDown() && m_hasCropRect) {
            ApplyCrop();
        }
        break;
    default:
        evt.Skip();
    }
}

void ImagePanel::SetCursorForPos(const wxPoint& screenPt) {
    if (!m_cropEnabled) {
        SetCursor(wxCursor(wxCURSOR_ARROW));
        return;
    }
    HandleID hit = HitTestHandle(screenPt);
    switch (hit) {
    case HandleID::NorthWest:
    case HandleID::SouthEast:
        SetCursor(m_cursorNWSW);
        break;
    case HandleID::NorthEast:
    case HandleID::SouthWest:
        SetCursor(m_cursorNESW);
        break;
    case HandleID::North:
    case HandleID::South:
        SetCursor(wxCursor(wxCURSOR_SIZENS));
        break;
    case HandleID::West:
    case HandleID::East:
        SetCursor(wxCursor(wxCURSOR_SIZEWE));
        break;
    case HandleID::Center:
        SetCursor(wxCursor(wxCURSOR_SIZING));
        break;
    default:
        SetCursor(wxCursor(wxCURSOR_CROSS));
        break;
    }
}

// ─── Paint ────────────────────────────────────────────────────────────────

void ImagePanel::OnPaint(wxPaintEvent&) {
    wxAutoBufferedPaintDC dc(this);
    dc.SetBackground(wxBrush(wxColour(40, 40, 40)));
    dc.Clear();

    if (!m_original.IsOk()) {
        dc.SetTextForeground(wxColour(150, 150, 150));
        dc.SetFont(wxFont(14, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL,
                          wxFONTWEIGHT_NORMAL));
        dc.DrawText("Drag an image here or use File → Open",
                    20, 20);
        return;
    }

    // Draw the scaled image
    if (m_displayBmp.IsOk()) {
        dc.DrawBitmap(m_displayBmp, m_imageRect.x, m_imageRect.y, false);
    }

    // Draw crop overlay
    if (m_hasCropRect && m_cropEnabled) {
        DrawCropOverlay(dc);
        DrawHandles(dc);
    }
}

void ImagePanel::DrawCropOverlay(wxDC& dc) {
    wxRect scrRect = ImageToScreen(m_cropRect);

    // Semi-transparent dark overlay outside crop rect
    wxColour dim(0, 0, 0, 150);
    wxBrush dimBrush(dim);
    wxPen noPen(*wxTRANSPARENT_PEN);

    dc.SetPen(noPen);
    dc.SetBrush(dimBrush);

    int l = m_imageRect.x;
    int t = m_imageRect.y;
    int r = m_imageRect.x + m_imageRect.width;
    int b = m_imageRect.y + m_imageRect.height;

    // Top
    dc.DrawRectangle(l, t, m_imageRect.width, scrRect.y - t);
    // Bottom
    dc.DrawRectangle(l, scrRect.y + scrRect.height,
                     m_imageRect.width, b - (scrRect.y + scrRect.height));
    // Left
    dc.DrawRectangle(l, scrRect.y, scrRect.x - l, scrRect.height);
    // Right
    dc.DrawRectangle(scrRect.x + scrRect.width, scrRect.y,
                     r - (scrRect.x + scrRect.width), scrRect.height);

    // Crop rectangle border — dashed "marching ants" effect
    wxPen dashPen(wxColour(255, 255, 255, 220), 1, wxPENSTYLE_SHORT_DASH);
    dc.SetPen(dashPen);
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    dc.DrawRectangle(scrRect);

    // Rule of thirds guides (subtle)
    wxPen guidePen(wxColour(255, 255, 255, 50), 1, wxPENSTYLE_DOT);
    dc.SetPen(guidePen);
    for (int i = 1; i < 3; i++) {
        int gx = scrRect.x + (scrRect.width  * i / 3);
        int gy = scrRect.y + (scrRect.height * i / 3);
        dc.DrawLine(gx, scrRect.y, gx, scrRect.y + scrRect.height);
        dc.DrawLine(scrRect.x, gy, scrRect.x + scrRect.width, gy);
    }
}

void ImagePanel::DrawHandles(wxDC& dc) {
    wxRect scr = ImageToScreen(m_cropRect);

    wxPoint pts[8] = {
        scr.GetTopLeft(),
        wxPoint(scr.x + scr.width/2, scr.y),
        scr.GetTopRight(),
        wxPoint(scr.x,               scr.y + scr.height/2),
        wxPoint(scr.x + scr.width,   scr.y + scr.height/2),
        scr.GetBottomLeft(),
        wxPoint(scr.x + scr.width/2, scr.y + scr.height),
        scr.GetBottomRight()
    };

    wxPen   handlePen(wxColour(255, 255, 255), 1);
    wxBrush handleBrush(wxColour(80, 80, 80));

    dc.SetPen(handlePen);
    dc.SetBrush(handleBrush);

    for (int i = 0; i < 8; i++) {
        dc.DrawRectangle(pts[i].x - HANDLE_HALF, pts[i].y - HANDLE_HALF,
                         HANDLE_SIZE, HANDLE_SIZE);
    }
}

void ImagePanel::OnSize(wxSizeEvent&) {
    RecalcLayout();
    RenderToBuffer();
    Refresh();
}
