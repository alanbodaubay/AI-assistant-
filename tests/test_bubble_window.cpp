#include <cassert>
#include <iostream>
#include "ui/bubble_window.h"

int main() {
    std::cout << "[ScreenMate Test] Initializing Dynamic BubbleWindow..." << std::endl;

    screenmate::ui::BubbleWindow bubble(380, 170, 480);
    bool ok = bubble.create();
    if (!ok || !bubble.hwnd()) {
        std::cerr << "BubbleWindow::create failed" << std::endl;
        return 1;
    }

    std::cout << "[ScreenMate Test] Testing show_near..." << std::endl;
    bubble.show_near(500, 500);
    assert(bubble.is_visible() && "BubbleWindow should be visible after show_near");

    int initial_height = bubble.height();
    assert(initial_height == 170 && "Initial height should match min_height");

    std::cout << "[ScreenMate Test] Testing dynamic height expansion with multi-line text..." << std::endl;
    std::string long_text =
        "Hey Alan, here is a detailed breakdown of what we are working on.\n"
        "First, we built the bioluminescent organic orb with per-pixel alpha transparency and smooth animations.\n"
        "Second, we built the dialogue speech bubble with dynamic layout resizing and global hotkeys.\n"
        "Third, we wired the async HTTP streaming pipe directly to llama-server slot 0 with prompt caching.\n"
        "Fourth, the entire pipeline runs locally on your NVIDIA RTX 4060 GPU with zero cloud latency.\n"
        "Fifth, the bubble dynamically expands upward as new tokens arrive so nothing ever gets clipped.\n"
        "Sixth, if the message exceeds maximum bounds, it smoothly anchors the viewport to the newest text.\n"
        "Finally, we are ready to connect the SQLite Soul graph brain and screen perception engine next!";

    bubble.set_response_text(long_text);
    int expanded_height = bubble.height();
    std::cout << "[ScreenMate Test] Initial height: " << initial_height << "px -> Expanded height: " << expanded_height << "px" << std::endl;
    assert(expanded_height > initial_height && "BubbleWindow height should dynamically expand for longer text");

    std::cout << "[ScreenMate Test] Testing reset to compact height..." << std::endl;
    bubble.set_response_text("Short reply.");
    assert(bubble.height() == initial_height && "BubbleWindow height should return to compact base height for short text");

    std::cout << "[ScreenMate Test] Testing hide..." << std::endl;
    bubble.hide();
    assert(!bubble.is_visible() && "BubbleWindow should not be visible after hide");

    std::cout << "[ScreenMate Test] PASS: All dynamic BubbleWindow assertions verified successfully." << std::endl;
    return 0;
}
