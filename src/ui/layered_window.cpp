#include "ui/layered_window.h"
#include <dwmapi.h>
#include <stdexcept>

namespace screenmate::ui {

LayeredWindow::LayeredWindow(int width, int height, const wchar_t* title)
    : width_(width), height_(height), x_(100), y_(100), title_(title),
      hwnd_(nullptr), hdc_mem_(nullptr), hbitmap_(nullptr), dib_pixels_(nullptr) {}

LayeredWindow::~LayeredWindow() {
    close();
}

LRESULT CALLBACK LayeredWindow::window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    LayeredWindow* window = nullptr;

    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCT*>(lparam);
        window = reinterpret_cast<LayeredWindow*>(cs->lpCreateParams);
        if (window) {
            window->hwnd_ = hwnd;
        }
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    } else {
        window = reinterpret_cast<LayeredWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (window) {
        return window->handle_message(msg, wparam, lparam);
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

bool LayeredWindow::create(int initial_x, int initial_y) {
    HINSTANCE hinstance = GetModuleHandleW(nullptr);
    const wchar_t* class_name = L"ScreenMateLayeredWindowClass";

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = LayeredWindow::window_proc;
    wc.hInstance = hinstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = class_name;
    RegisterClassExW(&wc);

    // Default position: bottom-right above taskbar
    if (initial_x < 0 || initial_y < 0) {
        RECT work_area = {};
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &work_area, 0);
        x_ = work_area.right - width_ - 40;
        y_ = work_area.bottom - height_ - 40;
    } else {
        x_ = initial_x;
        y_ = initial_y;
    }

    DWORD ex_style = WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW;
    DWORD style = WS_POPUP;

    hwnd_ = CreateWindowExW(
        ex_style,
        class_name,
        title_,
        style,
        x_,
        y_,
        width_,
        height_,
        nullptr,
        nullptr,
        hinstance,
        this
    );

    if (!hwnd_) {
        return false;
    }

    // Exclude window from screen capture so it never sees itself
    SetWindowDisplayAffinity(hwnd_, WDA_EXCLUDEFROMCAPTURE);

    setup_memory_dc();
    SetTimer(hwnd_, 1, 33, nullptr); // 30 FPS animation timer during modal dragging
    return true;
}

void LayeredWindow::setup_memory_dc() {
    HDC screen_dc = GetDC(nullptr);
    hdc_mem_ = CreateCompatibleDC(screen_dc);

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width_;
    bmi.bmiHeader.biHeight = -height_; // Negative for top-down DIB
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    hbitmap_ = CreateDIBSection(hdc_mem_, &bmi, DIB_RGB_COLORS, &dib_pixels_, nullptr, 0);
    SelectObject(hdc_mem_, hbitmap_);

    ReleaseDC(nullptr, screen_dc);
}

void LayeredWindow::release_memory_dc() {
    if (hbitmap_) {
        DeleteObject(hbitmap_);
        hbitmap_ = nullptr;
    }
    if (hdc_mem_) {
        DeleteDC(hdc_mem_);
        hdc_mem_ = nullptr;
    }
    dib_pixels_ = nullptr;
}

void LayeredWindow::show() {
    if (hwnd_) {
        SetWindowPos(hwnd_, HWND_TOPMOST, x_, y_, width_, height_, SWP_SHOWWINDOW);
        UpdateWindow(hwnd_);
    }
}

void LayeredWindow::hide() {
    if (hwnd_) {
        ShowWindow(hwnd_, SW_HIDE);
    }
}

void LayeredWindow::close() {
    release_memory_dc();
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
}

void LayeredWindow::set_position(int x, int y) {
    x_ = x;
    y_ = y;
    if (hwnd_) {
        SetWindowPos(hwnd_, HWND_TOPMOST, x_, y_, width_, height_, SWP_NOSIZE | SWP_NOACTIVATE);
    }
}

bool LayeredWindow::present(const renderer::BgraBuffer& buffer) {
    if (!hwnd_ || !hdc_mem_ || !dib_pixels_) return false;

    // Copy buffer bytes into DIB section
    size_t copy_size = std::min(buffer.size_bytes(), static_cast<size_t>(width_ * height_ * 4));
    std::memcpy(dib_pixels_, buffer.data(), copy_size);

    POINT pt_src = { 0, 0 };
    SIZE size_wnd = { width_, height_ };
    POINT pt_dst = { x_, y_ };

    BLENDFUNCTION blend = {};
    blend.BlendOp = AC_SRC_OVER;
    blend.BlendFlags = 0;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = AC_SRC_ALPHA; // Premultiplied per-pixel alpha

    HDC screen_dc = GetDC(nullptr);
    BOOL ok = UpdateLayeredWindow(
        hwnd_,
        screen_dc,
        &pt_dst,
        &size_wnd,
        hdc_mem_,
        &pt_src,
        0,
        &blend,
        ULW_ALPHA
    );
    ReleaseDC(nullptr, screen_dc);

    return ok == TRUE;
}

LRESULT LayeredWindow::handle_message(UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
        case WM_LBUTTONDOWN: {
            // Native smooth window dragging
            ReleaseCapture();
            SendMessageW(hwnd_, WM_NCLBUTTONDOWN, HTCAPTION, 0);
            RECT r = {};
            GetWindowRect(hwnd_, &r);
            x_ = r.left;
            y_ = r.top;
            return 0;
        }
        case WM_MOVE: {
            RECT r = {};
            GetWindowRect(hwnd_, &r);
            x_ = r.left;
            y_ = r.top;
            return 0;
        }
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(hwnd_, msg, wparam, lparam);
    }
}

} // namespace screenmate::ui

