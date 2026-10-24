#include "ui/blob_window.h"

namespace screenmate::ui {

BlobWindow::BlobWindow(int dimension)
    : LayeredWindow(dimension, dimension, L"ScreenMate Blob"),
      renderer_(dimension),
      buffer_(dimension, dimension),
      current_emotion_(renderer::BlobEmotion::Calm),
      activity_level_(0.0f),
      start_time_(std::chrono::steady_clock::now()) {}

void BlobWindow::update_frame(double time_seconds) {
    renderer_.render(buffer_, time_seconds, current_emotion_, activity_level_);
    present(buffer_);
}

void BlobWindow::set_emotion(renderer::BlobEmotion emotion) {
    current_emotion_ = emotion;
}

void BlobWindow::set_activity_level(float activity) {
    activity_level_ = activity;
}

void BlobWindow::cycle_emotion() {
    switch (current_emotion_) {
        case renderer::BlobEmotion::Calm:
            current_emotion_ = renderer::BlobEmotion::Focused;
            break;
        case renderer::BlobEmotion::Focused:
            current_emotion_ = renderer::BlobEmotion::Witty;
            break;
        case renderer::BlobEmotion::Witty:
            current_emotion_ = renderer::BlobEmotion::Thinking;
            break;
        case renderer::BlobEmotion::Thinking:
            current_emotion_ = renderer::BlobEmotion::Alert;
            break;
        case renderer::BlobEmotion::Alert:
            current_emotion_ = renderer::BlobEmotion::Calm;
            break;
    }
}

LRESULT BlobWindow::handle_message(UINT msg, WPARAM wparam, LPARAM lparam) {
    if (msg == WM_TIMER) {
        auto now = std::chrono::steady_clock::now();
        double elapsed_sec = std::chrono::duration<double>(now - start_time_).count();
        update_frame(elapsed_sec);
        return 0;
    }

    switch (msg) {
        case WM_RBUTTONUP: {
            // Right-click context menu: Cycle mood or Exit
            HMENU menu = CreatePopupMenu();
            AppendMenuW(menu, MF_STRING, 1001, L"Cycle Emotion (Mood)");
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING, 1002, L"Exit ScreenMate");

            POINT pt;
            GetCursorPos(&pt);
            SetForegroundWindow(hwnd());
            int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd(), nullptr);
            DestroyMenu(menu);

            if (cmd == 1001) {
                cycle_emotion();
            } else if (cmd == 1002) {
                PostQuitMessage(0);
            }
            return 0;
        }
        case WM_LBUTTONDBLCLK: {
            cycle_emotion();
            return 0;
        }
        default:
            return LayeredWindow::handle_message(msg, wparam, lparam);
    }
}

} // namespace screenmate::ui

