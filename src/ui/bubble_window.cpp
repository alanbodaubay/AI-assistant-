#include "ui/bubble_window.h"
#include <algorithm>
#include <vector>

namespace screenmate::ui {

namespace {
constexpr UINT WM_APP_APPEND_TOKEN = WM_APP + 201;
constexpr UINT WM_APP_SET_TEXT = WM_APP + 202;

std::wstring utf8_to_wide(const std::string& str) {
    if (str.empty()) return L"";
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), nullptr, 0);
    std::wstring wstr(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), &wstr[0], size_needed);
    return wstr;
}

std::string wide_to_utf8(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), nullptr, 0, nullptr, nullptr);
    std::string str(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), &str[0], size_needed, nullptr, nullptr);
    return str;
}
} // namespace

BubbleWindow::BubbleWindow(int width, int min_height, int max_height)
    : width_(width),
      height_(min_height),
      min_height_(min_height),
      max_height_(max_height),
      anchor_x_(500),
      anchor_y_(500),
      hwnd_(nullptr),
      edit_hwnd_(nullptr),
      font_body_(nullptr),
      font_title_(nullptr),
      font_input_(nullptr),
      bg_brush_(nullptr),
      edit_bg_brush_(nullptr),
      old_edit_proc_(nullptr),
      current_response_("Hey Alan! Press Ctrl+Space anytime to talk with me.") {}

BubbleWindow::~BubbleWindow() {
    hide();
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    if (font_body_) DeleteObject(font_body_);
    if (font_title_) DeleteObject(font_title_);
    if (font_input_) DeleteObject(font_input_);
    if (bg_brush_) DeleteObject(bg_brush_);
    if (edit_bg_brush_) DeleteObject(edit_bg_brush_);
}

LRESULT CALLBACK BubbleWindow::window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    BubbleWindow* window = nullptr;

    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCT*>(lparam);
        window = reinterpret_cast<BubbleWindow*>(cs->lpCreateParams);
        if (window) {
            window->hwnd_ = hwnd;
        }
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    } else {
        window = reinterpret_cast<BubbleWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (window) {
        return window->handle_message(msg, wparam, lparam);
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

LRESULT CALLBACK BubbleWindow::edit_subclass_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    auto* self = reinterpret_cast<BubbleWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self) {
        if (msg == WM_KEYDOWN) {
            if (wparam == VK_RETURN) {
                wchar_t buf[2048] = {};
                GetWindowTextW(hwnd, buf, 2048);
                SetWindowTextW(hwnd, L"");

                std::wstring wide_input(buf);
                std::string utf8_input = wide_to_utf8(wide_input);

                if (!utf8_input.empty()) {
                    self->set_response_text("Thinking...");
                    if (self->on_submit_) {
                        self->on_submit_(utf8_input);
                    }
                }
                return 0;
            } else if (wparam == VK_ESCAPE) {
                self->hide();
                return 0;
            }
        }
    }

    if (self && self->old_edit_proc_) {
        return CallWindowProcW(self->old_edit_proc_, hwnd, msg, wparam, lparam);
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

bool BubbleWindow::create(HWND parent_hwnd) {
    HINSTANCE hinstance = GetModuleHandleW(nullptr);
    const wchar_t* class_name = L"ScreenMateBubbleClass";

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = BubbleWindow::window_proc;
    wc.hInstance = hinstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = class_name;
    RegisterClassExW(&wc);

    DWORD ex_style = WS_EX_TOPMOST | WS_EX_TOOLWINDOW;
    DWORD style = WS_POPUP;

    hwnd_ = CreateWindowExW(
        ex_style,
        class_name,
        L"ScreenMate Dialogue",
        style,
        100,
        100,
        width_,
        height_,
        parent_hwnd,
        nullptr,
        hinstance,
        this
    );

    if (!hwnd_) {
        return false;
    }

    // Exclude speech bubble from screen captures for privacy
    SetWindowDisplayAffinity(hwnd_, WDA_EXCLUDEFROMCAPTURE);

    // Apply rounded window region
    HRGN rgn = CreateRoundRectRgn(0, 0, width_ + 1, height_ + 1, 14, 14);
    SetWindowRgn(hwnd_, rgn, TRUE);

    // Create dark UI resources
    bg_brush_ = CreateSolidBrush(RGB(20, 20, 26));
    edit_bg_brush_ = CreateSolidBrush(RGB(32, 32, 42));

    font_title_ = CreateFontW(
        -11, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"
    );

    font_body_ = CreateFontW(
        -13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"
    );

    font_input_ = CreateFontW(
        -13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"
    );

    // Create child edit control for message input
    int input_y = height_ - 36;
    int input_h = 24;
    int input_x = 16;
    int input_w = width_ - 32;

    edit_hwnd_ = CreateWindowExW(
        0,
        L"EDIT",
        L"",
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_LEFT,
        input_x,
        input_y,
        input_w,
        input_h,
        hwnd_,
        reinterpret_cast<HMENU>(101),
        hinstance,
        nullptr
    );

    if (edit_hwnd_) {
        SendMessageW(edit_hwnd_, WM_SETFONT, reinterpret_cast<WPARAM>(font_input_), TRUE);
        SetWindowLongPtrW(edit_hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
        old_edit_proc_ = reinterpret_cast<WNDPROC>(
            SetWindowLongPtrW(edit_hwnd_, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(edit_subclass_proc))
        );
    }

    return true;
}

void BubbleWindow::show_near(int anchor_x, int anchor_y) {
    anchor_x_ = anchor_x;
    anchor_y_ = anchor_y;
    reposition_window();
    focus_input();
}

void BubbleWindow::reposition_window() {
    if (!hwnd_) return;

    // Center speech bubble horizontally relative to the 128px blob
    int target_x = anchor_x_ + (128 - width_) / 2;
    // Anchor bottom of speech bubble 12px above the top of the blob
    int target_y = anchor_y_ - height_ - 12;

    // Clamp inside work area to never render offscreen
    RECT work_area = {};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work_area, 0);

    if (target_y < work_area.top + 8) {
        // Blob is too close to top of screen; show below blob
        target_y = anchor_y_ + 128 + 12;
    }

    target_x = (std::max)(static_cast<int>(work_area.left + 8), (std::min)(target_x, static_cast<int>(work_area.right - width_ - 8)));
    target_y = (std::max)(static_cast<int>(work_area.top + 8), (std::min)(target_y, static_cast<int>(work_area.bottom - height_ - 8)));

    SetWindowPos(hwnd_, HWND_TOPMOST, target_x, target_y, width_, height_, SWP_SHOWWINDOW);
}

void BubbleWindow::update_dynamic_layout() {
    if (!hwnd_) return;

    // Measure required text height
    std::wstring wresponse = utf8_to_wide(current_response_);
    HDC screen_dc = GetDC(hwnd_);
    HDC mem_dc = CreateCompatibleDC(screen_dc);
    HGDIOBJ old_font = SelectObject(mem_dc, font_body_);

    RECT calc_rect = { 0, 0, width_ - 32, 0 };
    DrawTextW(mem_dc, wresponse.c_str(), -1, &calc_rect, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    int text_h = calc_rect.bottom - calc_rect.top;

    SelectObject(mem_dc, old_font);
    DeleteDC(mem_dc);
    ReleaseDC(hwnd_, screen_dc);

    // Layout overhead: header (32px) + bottom input margins (52px) + padding (10px) = 94px
    int needed_height = text_h + 94;
    int target_height = (std::max)(min_height_, (std::min)(needed_height, max_height_));

    if (target_height != height_) {
        height_ = target_height;

        // Apply updated rounded window region for new height
        HRGN rgn = CreateRoundRectRgn(0, 0, width_ + 1, height_ + 1, 14, 14);
        SetWindowRgn(hwnd_, rgn, TRUE);

        // Reposition child edit control to stay anchored at bottom
        int input_y = height_ - 36;
        int input_w = width_ - 32;
        if (edit_hwnd_) {
            SetWindowPos(edit_hwnd_, nullptr, 16, input_y, input_w, 24, SWP_NOZORDER | SWP_NOACTIVATE);
        }

        // Adjust window position so expansion happens upward from the anchor
        reposition_window();
    }

    InvalidateRect(hwnd_, nullptr, FALSE);
}

void BubbleWindow::hide() {
    if (hwnd_) {
        ShowWindow(hwnd_, SW_HIDE);
    }
}

bool BubbleWindow::is_visible() const noexcept {
    return hwnd_ && IsWindowVisible(hwnd_);
}

void BubbleWindow::focus_input() {
    if (hwnd_ && edit_hwnd_) {
        SetForegroundWindow(hwnd_);
        SetFocus(edit_hwnd_);
    }
}

void BubbleWindow::set_on_submit(std::function<void(const std::string& text)> on_submit) {
    on_submit_ = std::move(on_submit);
}

void BubbleWindow::set_response_text(const std::string& text) {
    if (!hwnd_) return;

    DWORD wnd_thread = GetWindowThreadProcessId(hwnd_, nullptr);
    if (GetCurrentThreadId() == wnd_thread) {
        current_response_ = text;
        update_dynamic_layout();
    } else {
        auto* copy = new std::string(text);
        PostMessageW(hwnd_, WM_APP_SET_TEXT, 0, reinterpret_cast<LPARAM>(copy));
    }
}

void BubbleWindow::append_response_token(const std::string& token) {
    if (!hwnd_) return;

    auto* copy = new std::string(token);
    PostMessageW(hwnd_, WM_APP_APPEND_TOKEN, 0, reinterpret_cast<LPARAM>(copy));
}

LRESULT BubbleWindow::handle_message(UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
        case WM_APP_APPEND_TOKEN: {
            auto* token = reinterpret_cast<std::string*>(lparam);
            if (token) {
                if (current_response_ == "Thinking...") {
                    current_response_.clear();
                }
                current_response_ += *token;
                delete token;
                update_dynamic_layout();
            }
            return 0;
        }

        case WM_APP_SET_TEXT: {
            auto* text = reinterpret_cast<std::string*>(lparam);
            if (text) {
                current_response_ = *text;
                delete text;
                update_dynamic_layout();
            }
            return 0;
        }

        case WM_CTLCOLOREDIT: {
            HDC hdc = reinterpret_cast<HDC>(wparam);
            SetTextColor(hdc, RGB(255, 255, 255));
            SetBkColor(hdc, RGB(32, 32, 42));
            return reinterpret_cast<LRESULT>(edit_bg_brush_);
        }

        case WM_ERASEBKGND:
            return 1; // Double-buffered in WM_PAINT

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd_, &ps);

            // Double-buffer memory DC
            HDC mem_dc = CreateCompatibleDC(hdc);
            HBITMAP mem_bm = CreateCompatibleBitmap(hdc, width_, height_);
            HGDIOBJ old_bm = SelectObject(mem_dc, mem_bm);

            // 1. Draw bubble background
            RECT full_rect = { 0, 0, width_, height_ };
            FillRect(mem_dc, &full_rect, bg_brush_);

            // 2. Draw border
            HPEN border_pen = CreatePen(PS_SOLID, 1, RGB(55, 55, 70));
            HGDIOBJ old_pen = SelectObject(mem_dc, border_pen);
            HGDIOBJ old_brush = SelectObject(mem_dc, GetStockObject(NULL_BRUSH));
            RoundRect(mem_dc, 0, 0, width_, height_, 14, 14);

            // 3. Header title / status
            SelectObject(mem_dc, font_title_);
            SetBkMode(mem_dc, TRANSPARENT);
            SetTextColor(mem_dc, RGB(160, 150, 220)); // Soft purple/violet header
            RECT title_rect = { 16, 10, width_ - 16, 26 };
            DrawTextW(mem_dc, L"ScreenMate", -1, &title_rect, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);

            // Subtle mood dot indicator
            HBRUSH dot_brush = CreateSolidBrush(RGB(80, 220, 160));
            SelectObject(mem_dc, dot_brush);
            SelectObject(mem_dc, GetStockObject(NULL_PEN));
            Ellipse(mem_dc, width_ - 26, 12, width_ - 18, 20);
            DeleteObject(dot_brush);

            // 4. Response text with automatic auto-scroll viewport
            SelectObject(mem_dc, font_body_);
            SetTextColor(mem_dc, RGB(232, 232, 240));

            int text_top = 32;
            int text_bottom = height_ - 44;
            int avail_h = text_bottom - text_top;

            std::wstring wresponse = utf8_to_wide(current_response_);
            RECT measure_rect = { 0, 0, width_ - 32, 0 };
            DrawTextW(mem_dc, wresponse.c_str(), -1, &measure_rect, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
            int measured_h = measure_rect.bottom - measure_rect.top;

            // If text exceeds maximum bubble height, smoothly anchor view to newest text
            int scroll_offset = 0;
            if (measured_h > avail_h) {
                scroll_offset = measured_h - avail_h;
            }

            HRGN clip_rgn = CreateRectRgn(16, text_top, width_ - 16, text_bottom);
            SelectClipRgn(mem_dc, clip_rgn);

            RECT draw_rect = { 16, text_top - scroll_offset, width_ - 16, text_top - scroll_offset + measured_h };
            DrawTextW(mem_dc, wresponse.c_str(), -1, &draw_rect, DT_WORDBREAK | DT_NOPREFIX);

            SelectClipRgn(mem_dc, nullptr);
            DeleteObject(clip_rgn);

            // 5. Input box container background
            RECT input_box_rect = { 12, height_ - 40, width_ - 12, height_ - 8 };
            HPEN input_border = CreatePen(PS_SOLID, 1, RGB(48, 48, 64));
            SelectObject(mem_dc, input_border);
            SelectObject(mem_dc, edit_bg_brush_);
            RoundRect(mem_dc, input_box_rect.left, input_box_rect.top, input_box_rect.right, input_box_rect.bottom, 8, 8);
            DeleteObject(input_border);

            // Clean up GDI objects
            SelectObject(mem_dc, old_pen);
            SelectObject(mem_dc, old_brush);
            DeleteObject(border_pen);

            BitBlt(hdc, 0, 0, width_, height_, mem_dc, 0, 0, SRCCOPY);
            SelectObject(mem_dc, old_bm);
            DeleteObject(mem_bm);
            DeleteDC(mem_dc);

            EndPaint(hwnd_, &ps);
            return 0;
        }

        default:
            return DefWindowProcW(hwnd_, msg, wparam, lparam);
    }
}

} // namespace screenmate::ui
