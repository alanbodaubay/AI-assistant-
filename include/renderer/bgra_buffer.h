#pragma once

#include <vector>
#include <cstdint>
#include <algorithm>
#include <cstring>

namespace screenmate::renderer {

/**
 * @brief 32-bit Premultiplied BGRA pixel buffer for Win32 UpdateLayeredWindow.
 * 
 * Pixel layout in memory:
 * Byte 0: Blue  (premultiplied)
 * Byte 1: Green (premultiplied)
 * Byte 2: Red   (premultiplied)
 * Byte 3: Alpha (0 to 255)
 */
class BgraBuffer {
public:
    BgraBuffer(int width, int height)
        : width_(width), height_(height), stride_(width * 4), pixels_(width * height * 4, 0) {}

    void resize(int width, int height) {
        width_ = width;
        height_ = height;
        stride_ = width * 4;
        pixels_.assign(width * height * 4, 0);
    }

    void clear() {
        std::memset(pixels_.data(), 0, pixels_.size());
    }

    int width() const noexcept { return width_; }
    int height() const noexcept { return height_; }
    int stride() const noexcept { return stride_; }
    const uint8_t* data() const noexcept { return pixels_.data(); }
    uint8_t* data() noexcept { return pixels_.data(); }
    size_t size_bytes() const noexcept { return pixels_.size(); }

    /**
     * @brief Writes a premultiplied BGRA pixel directly.
     */
    inline void set_pixel_premultiplied(int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a) noexcept {
        if (x < 0 || x >= width_ || y < 0 || y >= height_) return;
        size_t idx = static_cast<size_t>((y * width_ + x) * 4);
        pixels_[idx + 0] = b;
        pixels_[idx + 1] = g;
        pixels_[idx + 2] = r;
        pixels_[idx + 3] = a;
    }

    /**
     * @brief Sets a pixel from straight (non-premultiplied) RGBA colors.
     * Computes premultiplied alpha on the fly: C_premul = (C * A) / 255.
     */
    inline void set_pixel_straight(int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a) noexcept {
        if (x < 0 || x >= width_ || y < 0 || y >= height_ || a == 0) return;
        size_t idx = static_cast<size_t>((y * width_ + x) * 4);
        pixels_[idx + 0] = static_cast<uint8_t>((static_cast<uint32_t>(b) * a) / 255);
        pixels_[idx + 1] = static_cast<uint8_t>((static_cast<uint32_t>(g) * a) / 255);
        pixels_[idx + 2] = static_cast<uint8_t>((static_cast<uint32_t>(r) * a) / 255);
        pixels_[idx + 3] = a;
    }

    /**
     * @brief Blends a straight RGBA pixel on top of existing premultiplied pixel.
     */
    inline void blend_pixel(int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a) noexcept {
        if (x < 0 || x >= width_ || y < 0 || y >= height_ || a == 0) return;
        size_t idx = static_cast<size_t>((y * width_ + x) * 4);

        uint32_t src_b = (static_cast<uint32_t>(b) * a) / 255;
        uint32_t src_g = (static_cast<uint32_t>(g) * a) / 255;
        uint32_t src_r = (static_cast<uint32_t>(r) * a) / 255;

        uint32_t inv_a = 255 - a;

        uint32_t dst_b = pixels_[idx + 0];
        uint32_t dst_g = pixels_[idx + 1];
        uint32_t dst_r = pixels_[idx + 2];
        uint32_t dst_a = pixels_[idx + 3];

        pixels_[idx + 0] = static_cast<uint8_t>(src_b + (dst_b * inv_a) / 255);
        pixels_[idx + 1] = static_cast<uint8_t>(src_g + (dst_g * inv_a) / 255);
        pixels_[idx + 2] = static_cast<uint8_t>(src_r + (dst_r * inv_a) / 255);
        pixels_[idx + 3] = static_cast<uint8_t>(a + (dst_a * inv_a) / 255);
    }

private:
    int width_;
    int height_;
    int stride_;
    std::vector<uint8_t> pixels_;
};

} // namespace screenmate::renderer

