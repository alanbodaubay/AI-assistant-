#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include <cstdint>

namespace screenmate::perception {

struct WindowInfo {
    HWND hwnd = nullptr;
    std::string title;
    std::string process_name;
    RECT rect{0, 0, 0, 0};
    bool is_valid = false;
};

struct CaptureResult {
    bool success = false;
    bool is_privacy_blocked = false;
    std::string privacy_reason;
    WindowInfo window;
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> jpeg_bytes;
    std::string base64_jpeg;
    uint64_t dhash = 0;
    int64_t timestamp_ms = 0;
};

class ScreenCapture {
public:
    explicit ScreenCapture(uint32_t max_dimension = 512, int jpeg_quality = 75);
    ~ScreenCapture();

    // Query active foreground window metadata without taking a screenshot
    WindowInfo GetForegroundWindowInfo() const;

    // Evaluates privacy blacklist against window title and process name
    bool IsPrivacyBlocked(const WindowInfo& info, std::string* out_reason = nullptr) const;

    // Captures the active foreground window, rescales, computes dHash, and encodes to JPEG
    CaptureResult CaptureForegroundWindow();

    // Captures a specific HWND
    CaptureResult CaptureWindow(HWND hwnd);

    // Computes 64-bit difference hash (dHash) from raw 32-bit BGRA/RGBA pixels
    static uint64_t ComputeDHash(const uint8_t* bgra_pixels, int width, int height, int stride);

    // Calculates normalized Hamming distance between two 64-bit hashes [0.0f, 1.0f]
    static float HashDifference(uint64_t hash1, uint64_t hash2);

    // Converts binary data to Base64
    static std::string Base64Encode(const uint8_t* data, size_t length);

private:
    uint32_t max_dimension_;
    int jpeg_quality_;
    ULONG_PTR gdiplus_token_ = 0;
    bool gdiplus_initialized_ = false;

    bool init_gdiplus();
    void shutdown_gdiplus();
};

} // namespace screenmate::perception

