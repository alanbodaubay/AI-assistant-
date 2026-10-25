#pragma once

#include <cstdint>
#include <string>
#include "perception/screen_capture.h"

namespace screenmate::perception {

struct GatekeeperConfig {
    double settle_time_seconds = 3.0;        // Minimum stable time before looking
    float dhash_change_threshold = 0.10f;     // 10% perceptual change required
    int64_t base_cooldown_seconds = 180;      // 3 minutes base interval between proactive remarks
    int64_t max_cooldown_seconds = 1200;      // 20 minutes max backoff
};

enum class GlanceDecision {
    Ready,                  // Screen changed and settled; cooldown elapsed -> proceed with proactive glance
    SuppressedByPrivacy,    // Active window is blacklisted
    SuppressedByMotion,     // Window changed recently, still within settle window
    SuppressedBySimilarity, // Screen hasn't changed enough (dHash diff < threshold)
    SuppressedByCooldown,   // Proactive cooldown has not elapsed yet
    SuppressedInvalidWindow // Foreground window invalid, desktop or minimized
};

class Gatekeeper {
public:
    explicit Gatekeeper(const GatekeeperConfig& config = GatekeeperConfig{});

    // Notify gatekeeper of current foreground window and screen dHash
    // Returns decision whether a proactive glance should fire
    GlanceDecision EvaluateProactiveGlance(const WindowInfo& window, uint64_t current_dhash, int64_t now_ms = 0);

    // Called when the user interacts or chats (resets backoff to base cooldown)
    void OnUserInteraction(int64_t now_ms = 0);

    // Called when ScreenMate gives a proactive remark that was ignored / dismissed
    void OnProactiveIgnored(int64_t now_ms = 0);

    // Called when a proactive remark is delivered
    void RecordProactiveDelivered(uint64_t dhash, int64_t now_ms = 0);

    // Force reset state
    void Reset();

    // Query state
    int64_t current_cooldown_seconds() const { return current_cooldown_seconds_; }
    uint64_t last_dhash() const { return last_dhash_; }
    HWND last_hwnd() const { return last_hwnd_; }
    int64_t last_glance_timestamp_ms() const { return last_glance_ms_; }
    int64_t last_window_switch_ms() const { return last_window_switch_ms_; }

private:
    GatekeeperConfig config_;
    HWND last_hwnd_ = nullptr;
    std::string last_title_;
    int64_t last_window_switch_ms_ = 0;
    uint64_t last_dhash_ = 0;
    int64_t last_glance_ms_ = 0;
    int64_t current_cooldown_seconds_;

    int64_t current_time_ms() const;
};

} // namespace screenmate::perception

