#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <chrono>
#include <thread>
#include "ui/blob_window.h"
#include "ui/bubble_window.h"
#include "brain/llama_client.h"

namespace {
constexpr int HOTKEY_ID = 100;
}

int WINAPI wWinMain(HINSTANCE /*hInstance*/, HINSTANCE /*hPrevInstance*/, LPWSTR /*lpCmdLine*/, int /*nCmdShow*/) {
    // 1. Create the glowing organic Blob of Light window (128x128 pixels)
    screenmate::ui::BlobWindow blob(128);
    if (!blob.create()) {
        MessageBoxW(nullptr, L"Failed to create ScreenMate Blob window.", L"Error", MB_ICONERROR);
        return 1;
    }

    // 2. Create the dialogue Speech Bubble window (360x190 pixels)
    screenmate::ui::BubbleWindow bubble(360, 190);
    if (!bubble.create(blob.hwnd())) {
        MessageBoxW(nullptr, L"Failed to create ScreenMate Bubble window.", L"Error", MB_ICONERROR);
        return 1;
    }

    // 3. Register Global Hotkey: Ctrl + Space
    RegisterHotKey(blob.hwnd(), HOTKEY_ID, MOD_CONTROL, VK_SPACE);

    // 4. Wire Blob interactions: Click to toggle speech bubble, drag to follow
    blob.set_on_click([&blob, &bubble]() {
        if (bubble.is_visible()) {
            bubble.hide();
        } else {
            bubble.show_near(blob.x(), blob.y());
        }
    });

    blob.set_on_move([&bubble](int x, int y) {
        if (bubble.is_visible()) {
            bubble.show_near(x, y);
        }
    });

    // 5. Wire Speech Bubble input: Async local sidecar inference via llama-server
    bubble.set_on_submit([&blob, &bubble](const std::string& user_text) {
        blob.set_emotion(screenmate::renderer::BlobEmotion::Thinking);

        std::thread([&blob, &bubble, user_text]() {
            screenmate::brain::LlamaClient client;
            std::string system_prompt =
                "You are ScreenMate, an intelligent, concise, witty personal companion and friend to Alan. "
                "You live directly on his desktop as a glowing organic companion. "
                "Keep answers natural, warm, thoughtful, and punchy. Avoid robotic boilerplate.";

            client.simple_chat(
                system_prompt,
                user_text,
                [&bubble](const std::string& token) {
                    bubble.append_response_token(token);
                }
            );

            blob.set_emotion(screenmate::renderer::BlobEmotion::Witty);
        }).detach();
    });

    // Render first frame so layered window has valid alpha pixels before display
    blob.update_frame(0.0);
    blob.show();

    auto start_time = std::chrono::steady_clock::now();
    MSG msg = {};
    bool running = true;

    // Gentle 30 FPS rendering loop (~33ms per frame = < 0.1% CPU usage)
    while (running) {
        // Process any pending Windows messages
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                running = false;
                break;
            }

            // Handle global hotkey Ctrl + Space
            if (msg.message == WM_HOTKEY && msg.wParam == HOTKEY_ID) {
                if (bubble.is_visible()) {
                    bubble.hide();
                } else {
                    bubble.show_near(blob.x(), blob.y());
                }
                continue;
            }

            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        if (!running) break;

        // Render current animated frame
        auto now = std::chrono::steady_clock::now();
        double elapsed_sec = std::chrono::duration<double>(now - start_time).count();
        blob.update_frame(elapsed_sec);

        // Sleep 33ms to ensure zero CPU fan spin
        std::this_thread::sleep_for(std::chrono::milliseconds(33));
    }

    UnregisterHotKey(blob.hwnd(), HOTKEY_ID);
    return 0;
}

// Fallback standard main for console/CLI launch
int main() {
    return wWinMain(GetModuleHandleW(nullptr), nullptr, nullptr, SW_SHOW);
}
