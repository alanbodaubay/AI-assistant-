#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <string>
#include <functional>

namespace screenmate::ui {

class BubbleWindow {
public:
    BubbleWindow(int width = 380, int min_height = 170, int max_height = 480);
    ~BubbleWindow();

    bool create(HWND parent_hwnd = nullptr);
    void show_near(int anchor_x, int anchor_y);
    void hide();
    bool is_visible() const noexcept;
    void focus_input();

    void set_on_submit(std::function<void(const std::string& text)> on_submit);
    void set_response_text(const std::string& text);
    void append_response_token(const std::string& token);

    HWND hwnd() const noexcept { return hwnd_; }
    HWND edit_hwnd() const noexcept { return edit_hwnd_; }
    int width() const noexcept { return width_; }
    int height() const noexcept { return height_; }

private:
    static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
    static LRESULT CALLBACK edit_subclass_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

    LRESULT handle_message(UINT msg, WPARAM wparam, LPARAM lparam);
    void update_dynamic_layout();
    void reposition_window();

    int width_;
    int height_;
    int min_height_;
    int max_height_;
    int anchor_x_;
    int anchor_y_;

    HWND hwnd_;
    HWND edit_hwnd_;
    HFONT font_body_;
    HFONT font_title_;
    HFONT font_input_;
    HBRUSH bg_brush_;
    HBRUSH edit_bg_brush_;
    WNDPROC old_edit_proc_;

    std::string current_response_;
    std::function<void(const std::string& text)> on_submit_;
};

} // namespace screenmate::ui
