#pragma once

#include <chrono>
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

protected:
    LRESULT handle_message(UINT msg, WPARAM wparam, LPARAM lparam) override;

private:
    renderer::BlobRenderer renderer_;
    renderer::BgraBuffer buffer_;
    renderer::BlobEmotion current_emotion_;
    float activity_level_;
    std::chrono::steady_clock::time_point start_time_;
};

} // namespace screenmate::ui

