#include <cassert>
#include <iostream>
#include "ui/bubble_window.h"

int main() {
    std::cout << "[ScreenMate Test] Initializing BubbleWindow..." << std::endl;

    screenmate::ui::BubbleWindow bubble(360, 190);
    assert(bubble.create() && "BubbleWindow::create failed");
    assert(bubble.hwnd() != nullptr && "BubbleWindow HWND must not be null");

    std::cout << "[ScreenMate Test] Testing show_near..." << std::endl;
    bubble.show_near(500, 500);
    assert(bubble.is_visible() && "BubbleWindow should be visible after show_near");

    std::cout << "[ScreenMate Test] Testing response text update..." << std::endl;
    bubble.set_response_text("Hello Alan! Milestone 2 is verified.");

    std::cout << "[ScreenMate Test] Testing hide..." << std::endl;
    bubble.hide();
    assert(!bubble.is_visible() && "BubbleWindow should not be visible after hide");

    std::cout << "[ScreenMate Test] PASS: All BubbleWindow assertions verified successfully." << std::endl;
    return 0;
}
