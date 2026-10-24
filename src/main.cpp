#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <chrono>
#include <thread>
#include "ui/blob_window.h"

int WINAPI wWinMain(HINSTANCE /*hInstance*/, HINSTANCE /*hPrevInstance*/, LPWSTR /*lpCmdLine*/, int /*nCmdShow*/) {
    // 1. Create the glowing organic Blob of Light window (128x128 pixels)
    screenmate::ui::BlobWindow blob(128);
    if (!blob.create()) {
        MessageBoxW(nullptr, L"Failed to create ScreenMate Blob window.", L"Error", MB_ICONERROR);
        return 1;
    }

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

    return 0;
}

// Fallback standard main for console/CLI launch
int main() {
    return wWinMain(GetModuleHandleW(nullptr), nullptr, nullptr, SW_SHOW);
}

