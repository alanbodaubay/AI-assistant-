#include <iostream>
#include <vector>
#include <cstdint>
#include <string>
#include "perception/screen_capture.h"
#include "perception/gatekeeper.h"

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAILED] " << msg << " (" << #cond << ") at line " << __LINE__ << std::endl; \
            return 1; \
        } \
    } while(0)

int main() {
    std::cout << "--- Running ScreenMate Perception & Gatekeeper Tests ---" << std::endl;

    // 1. Test Base64 Encoding
    {
        std::string text = "Hello World";
        std::string b64 = screenmate::perception::ScreenCapture::Base64Encode(
            reinterpret_cast<const uint8_t*>(text.data()), text.size());
        TEST_CHECK(b64 == "SGVsbG8gV29ybGQ=", "Base64 standard test failed");

        std::string empty = "";
        std::string b64_empty = screenmate::perception::ScreenCapture::Base64Encode(
            reinterpret_cast<const uint8_t*>(empty.data()), empty.size());
        TEST_CHECK(b64_empty == "", "Base64 empty test failed");
        std::cout << "[PASS] Base64 encoding validated." << std::endl;
    }

    // 2. Test dHash and Hamming Difference
    {
        // Identical hashes
        uint64_t h1 = 0xAAAAAAAAAAAAAAAAULL;
        uint64_t h2 = 0xAAAAAAAAAAAAAAAAULL;
        float diff_identical = screenmate::perception::ScreenCapture::HashDifference(h1, h2);
        TEST_CHECK(diff_identical == 0.0f, "Identical hash diff must be 0.0");

        // Fully inverted hashes
        uint64_t h_inv = ~h1;
        float diff_inverted = screenmate::perception::ScreenCapture::HashDifference(h1, h_inv);
        TEST_CHECK(diff_inverted == 1.0f, "Inverted hash diff must be 1.0");

        // 6 bits different out of 64
        uint64_t h_6diff = h1 ^ 0x3FULL;
        float diff_6 = screenmate::perception::ScreenCapture::HashDifference(h1, h_6diff);
        TEST_CHECK(diff_6 == 6.0f / 64.0f, "6-bit diff must equal 6/64");

        // Synthetic 9x8 image: strictly decreasing horizontal ramp -> all bits 1
        std::vector<uint8_t> decreasing_img(9 * 8 * 4, 0);
        for (int y = 0; y < 8; ++y) {
            for (int x = 0; x < 9; ++x) {
                uint8_t val = static_cast<uint8_t>(200 - (x * 20)); // decreasing left to right
                decreasing_img[(y * 9 + x) * 4 + 0] = val; // B
                decreasing_img[(y * 9 + x) * 4 + 1] = val; // G
                decreasing_img[(y * 9 + x) * 4 + 2] = val; // R
                decreasing_img[(y * 9 + x) * 4 + 3] = 255; // A
            }
        }
        uint64_t dhash_decreasing = screenmate::perception::ScreenCapture::ComputeDHash(
            decreasing_img.data(), 9, 8, 9 * 4);
        TEST_CHECK(dhash_decreasing == 0xFFFFFFFFFFFFFFFFULL, "Decreasing ramp must produce all 1s in dHash");

        std::cout << "[PASS] dHash computation and Hamming difference math validated." << std::endl;
    }

    // 3. Test Privacy Filter Blacklist
    {
        screenmate::perception::ScreenCapture capture(512);

        screenmate::perception::WindowInfo safe_win;
        safe_win.hwnd = reinterpret_cast<HWND>(1);
        safe_win.is_valid = true;
        safe_win.title = "Visual Studio Code - hopeful-planck";
        safe_win.process_name = "C:\\Tools\\Code.exe";
        std::string reason;
        TEST_CHECK(!capture.IsPrivacyBlocked(safe_win, &reason), "Safe developer window must not be blocked");

        screenmate::perception::WindowInfo bank_win;
        bank_win.hwnd = reinterpret_cast<HWND>(2);
        bank_win.is_valid = true;
        bank_win.title = "Chase Online Banking - Chrome";
        bank_win.process_name = "C:\\Program Files\\Google\\Chrome\\chrome.exe";
        TEST_CHECK(capture.IsPrivacyBlocked(bank_win, &reason), "Banking window must be blocked");

        screenmate::perception::WindowInfo pwd_win;
        pwd_win.hwnd = reinterpret_cast<HWND>(3);
        pwd_win.is_valid = true;
        pwd_win.title = "Bitwarden Password Manager";
        pwd_win.process_name = "C:\\Program Files\\Bitwarden\\Bitwarden.exe";
        TEST_CHECK(capture.IsPrivacyBlocked(pwd_win, &reason), "Password manager window must be blocked");

        screenmate::perception::WindowInfo incognito_win;
        incognito_win.hwnd = reinterpret_cast<HWND>(4);
        incognito_win.is_valid = true;
        incognito_win.title = "New Incognito Tab - Google Chrome";
        incognito_win.process_name = "C:\\Program Files\\Google\\Chrome\\chrome.exe";
        TEST_CHECK(capture.IsPrivacyBlocked(incognito_win, &reason), "Incognito window must be blocked");

        std::cout << "[PASS] Privacy blacklist filter verified across titles and processes." << std::endl;
    }

    // 4. Test Gatekeeper State Machine (Settle Timer, Similarity Filter, Cooldown Backoff)
    {
        screenmate::perception::GatekeeperConfig cfg;
        cfg.settle_time_seconds = 3.0;
        cfg.dhash_change_threshold = 0.10f; // 10%
        cfg.base_cooldown_seconds = 180;    // 3 min
        cfg.max_cooldown_seconds = 1200;    // 20 min

        screenmate::perception::Gatekeeper gatekeeper(cfg);

        screenmate::perception::WindowInfo win1;
        win1.hwnd = reinterpret_cast<HWND>(100);
        win1.is_valid = true;
        win1.title = "Text Editor - main.cpp";

        int64_t t0 = 1000000; // t = 0 ms

        // A. Window switch detected at t0 -> SuppressedByMotion
        auto d1 = gatekeeper.EvaluateProactiveGlance(win1, 0x1111222233334444ULL, t0);
        TEST_CHECK(d1 == screenmate::perception::GlanceDecision::SuppressedByMotion,
                   "Window switch must return SuppressedByMotion");

        // B. Query at t0 + 2000ms (< 3.0s settle timer) -> SuppressedByMotion
        auto d2 = gatekeeper.EvaluateProactiveGlance(win1, 0x1111222233334444ULL, t0 + 2000);
        TEST_CHECK(d2 == screenmate::perception::GlanceDecision::SuppressedByMotion,
                   "Query within 2.0s must stay SuppressedByMotion");

        // C. Query at t0 + 3500ms (> 3.0s settle timer, first glance, no previous dHash) -> Ready
        auto d3 = gatekeeper.EvaluateProactiveGlance(win1, 0x1111222233334444ULL, t0 + 3500);
        TEST_CHECK(d3 == screenmate::perception::GlanceDecision::Ready,
                   "Settled screen with initial hash must be Ready");

        // Record glance delivered
        gatekeeper.RecordProactiveDelivered(0x1111222233334444ULL, t0 + 3500);

        // D. Query at t0 + 4000ms -> SuppressedByCooldown (cooldown is 180s = 180,000ms)
        auto d4 = gatekeeper.EvaluateProactiveGlance(win1, 0x5555666677778888ULL, t0 + 4000);
        TEST_CHECK(d4 == screenmate::perception::GlanceDecision::SuppressedByCooldown,
                   "Query during active cooldown must return SuppressedByCooldown");

        // E. Query at t0 + 200,000ms (> 180s) with SAME dHash (difference 0% < 10%) -> SuppressedBySimilarity
        auto d5 = gatekeeper.EvaluateProactiveGlance(win1, 0x1111222233334444ULL, t0 + 200000);
        TEST_CHECK(d5 == screenmate::perception::GlanceDecision::SuppressedBySimilarity,
                   "Identical screen after cooldown must return SuppressedBySimilarity");

        // F. Query at t0 + 200,000ms with CHANGED dHash (>10%) -> Ready
        // Inverting 16 bits of the 64 bits = 25% difference
        uint64_t changed_dhash = 0x1111222233334444ULL ^ 0xFFFF000000000000ULL;
        auto d6 = gatekeeper.EvaluateProactiveGlance(win1, changed_dhash, t0 + 200000);
        TEST_CHECK(d6 == screenmate::perception::GlanceDecision::Ready,
                   "Changed settled screen after cooldown must be Ready");

        // Deliver second glance
        gatekeeper.RecordProactiveDelivered(changed_dhash, t0 + 200000);

        // G. User ignores proactive remark -> Backoff cooldown doubles (180s -> 360s)
        TEST_CHECK(gatekeeper.current_cooldown_seconds() == 180, "Base cooldown starts at 180s");
        gatekeeper.OnProactiveIgnored(t0 + 210000);
        TEST_CHECK(gatekeeper.current_cooldown_seconds() == 360, "Cooldown must double to 360s after being ignored");

        gatekeeper.OnProactiveIgnored(t0 + 220000);
        TEST_CHECK(gatekeeper.current_cooldown_seconds() == 720, "Cooldown must double to 720s after second ignore");

        // H. User initiates dialogue -> Cooldown resets to base (180s)
        gatekeeper.OnUserInteraction(t0 + 230000);
        TEST_CHECK(gatekeeper.current_cooldown_seconds() == 180, "User interaction must reset cooldown to 180s");

        std::cout << "[PASS] Gatekeeper state machine, settle timing, similarity suppression, and exponential backoff validated." << std::endl;
    }

    std::cout << "\n=== ALL SCREEN PERCEPTION & GATEKEEPER TESTS PASSED CLEANLY ===" << std::endl;
    return 0;
}

