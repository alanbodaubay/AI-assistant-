#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include "renderer/bgra_buffer.h"

namespace screenmate::ui {

class LayeredWindow {
public:
    LayeredWindow(int width, int height, const wchar_t* title = L"ScreenMate");
    virtual ~LayeredWindow();

    bool create(int initial_x = -1, int initial_y = -1);
    void show();
    void hide();
    void close();

    /**
     * @brief Updates the window content from a 32-bit premultiplied BGRA buffer.
     */
    bool present(const renderer::BgraBuffer& buffer);

    HWND hwnd() const noexcept { return hwnd_; }
    int width() const noexcept { return width_; }
    int height() const noexcept { return height_; }
    int x() const noexcept { return x_; }
    int y() const noexcept { return y_; }

    void set_position(int x, int y);

protected:
    virtual LRESULT handle_message(UINT msg, WPARAM wparam, LPARAM lparam);

private:
    static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
    void setup_memory_dc();
    void release_memory_dc();

    int width_;
    int height_;
    int x_;
    int y_;
    const wchar_t* title_;

    HWND hwnd_;
    HDC hdc_mem_;
    HBITMAP hbitmap_;
    void* dib_pixels_;
};

} // namespace screenmate::ui

