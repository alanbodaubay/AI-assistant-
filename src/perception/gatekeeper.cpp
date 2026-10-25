#include "perception/gatekeeper.h"
#include <chrono>
#include <algorithm>

namespace screenmate::perception {

Gatekeeper::Gatekeeper(const GatekeeperConfig& config)
    : config_(config), current_cooldown_seconds_(config.base_cooldown_seconds) {}

int64_t Gatekeeper::current_time_ms() const {
    auto now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
}

GlanceDecision Gatekeeper::EvaluateProactiveGlance(const WindowInfo& window, uint64_t current_dhash, int64_t now_ms) {
    if (now_ms <= 0) {
        now_ms = current_time_ms();
    }

    if (!window.is_valid || window.hwnd == nullptr) {
        return GlanceDecision::SuppressedInvalidWindow;
    }

    // 1. Detect foreground window switch
    if (last_hwnd_ != window.hwnd || last_title_ != window.title) {
        last_hwnd_ = window.hwnd;
        last_title_ = window.title;
        last_window_switch_ms_ = now_ms;
        return GlanceDecision::SuppressedByMotion;
    }

    // 2. Settle timer (must remain stable for settle_time_seconds)
    int64_t settle_ms = static_cast<int64_t>(config_.settle_time_seconds * 1000.0);
    if ((now_ms - last_window_switch_ms_) < settle_ms) {
        return GlanceDecision::SuppressedByMotion;
    }

    // 3. dHash perceptual similarity filter (must change by at least dhash_change_threshold)
    if (last_dhash_ != 0 && current_dhash != 0) {
        float diff = ScreenCapture::HashDifference(last_dhash_, current_dhash);
        if (diff < config_.dhash_change_threshold) {
            return GlanceDecision::SuppressedBySimilarity;
        }
    }

    // 4. Token bucket / proactive cooldown check
    int64_t cooldown_ms = current_cooldown_seconds_ * 1000;
    if ((now_ms - last_glance_ms_) < cooldown_ms) {
        return GlanceDecision::SuppressedByCooldown;
    }

    return GlanceDecision::Ready;
}

void Gatekeeper::OnUserInteraction(int64_t /*now_ms*/) {
    // Reset backoff to base cooldown when user engages in dialogue
    current_cooldown_seconds_ = config_.base_cooldown_seconds;
}

void Gatekeeper::OnProactiveIgnored(int64_t /*now_ms*/) {
    // Exponential backoff to avoid Clippy syndrome
    current_cooldown_seconds_ = std::min(current_cooldown_seconds_ * 2, config_.max_cooldown_seconds);
}

void Gatekeeper::RecordProactiveDelivered(uint64_t dhash, int64_t now_ms) {
    if (now_ms <= 0) {
        now_ms = current_time_ms();
    }
    last_glance_ms_ = now_ms;
    if (dhash != 0) {
        last_dhash_ = dhash;
    }
}

void Gatekeeper::Reset() {
    last_hwnd_ = nullptr;
    last_title_.clear();
    last_window_switch_ms_ = 0;
    last_dhash_ = 0;
    last_glance_ms_ = 0;
    current_cooldown_seconds_ = config_.base_cooldown_seconds;
}

} // namespace screenmate::perception

