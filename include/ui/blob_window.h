#pragma once

#include <chrono>
#include <functional>
#include "ui/layered_window.h"
#include "renderer/blob_renderer.h"

namespace screenmate::ui {

class BlobWindow : public LayeredWindow {
public:
    BlobWindow(int dimension = 128);

    void update_frame(double time_seconds);
    void cycle_emotion();
    void set_emotion(renderer::BlobEmotion emotion);
    void set_activity_level(float activity);

    void set_on_click(std::function<void()> cb);
    void set_on_move(std::function<void(int x, int y)> cb);

protected:
    LRESULT handle_message(UINT msg, WPARAM wparam, LPARAM lparam) override;
    void on_click() override;
    void on_move(int x, int y) override;

private:
    renderer::BlobRenderer renderer_;
    renderer::BgraBuffer buffer_;
    renderer::BlobEmotion current_emotion_;
    float activity_level_;
    std::chrono::steady_clock::time_point start_time_;
    std::function<void()> on_click_cb_;
    std::function<void(int x, int y)> on_move_cb_;
};

} // namespace screenmate::ui

