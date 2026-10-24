#include "renderer/blob_renderer.h"
#include <algorithm>
#include <numbers>

namespace screenmate::renderer {

BlobRenderer::BlobRenderer(int dimension)
    : dimension_(dimension) {}

void BlobRenderer::set_dimension(int dimension) {
    dimension_ = dimension;
}

BlobColor BlobRenderer::get_emotion_color(BlobEmotion emotion) const noexcept {
    switch (emotion) {
        case BlobEmotion::Calm:
            return { 40, 190, 255 };   // Radiant Cyan / Azure
        case BlobEmotion::Focused:
            return { 30, 225, 170 };   // Luminous Emerald
        case BlobEmotion::Witty:
            return { 255, 195, 45 };   // Warm Gold
        case BlobEmotion::Thinking:
            return { 185, 90, 255 };   // Deep Violet / Amethyst
        case BlobEmotion::Alert:
            return { 255, 95, 95 };    // Soft Coral
        default:
            return { 40, 190, 255 };
    }
}

void BlobRenderer::render(BgraBuffer& buffer, double time_seconds, BlobEmotion emotion, float activity_level) {
    buffer.clear();

    const float cx = dimension_ * 0.5f;
    const float cy = dimension_ * 0.5f;

    // Organic breathing frequency and harmonic pulsation
    const float base_freq = 1.6f + activity_level * 1.5f;
    const float t = static_cast<float>(time_seconds) * base_freq;

    // Radius breathing math
    const float pulse = std::sin(t) * 0.08f + std::sin(t * 2.3f) * 0.03f;
    const float base_radius = dimension_ * (0.28f + pulse + activity_level * 0.05f);
    const float aura_radius = base_radius * 1.9f;

    const BlobColor color = get_emotion_color(emotion);

    // Render radial glow fields
    const int min_x = std::max(0, static_cast<int>(cx - aura_radius));
    const int max_x = std::min(dimension_ - 1, static_cast<int>(cx + aura_radius));
    const int min_y = std::max(0, static_cast<int>(cy - aura_radius));
    const int max_y = std::min(dimension_ - 1, static_cast<int>(cy + aura_radius));

    for (int y = min_y; y <= max_y; ++y) {
        const float dy = y - cy;
        const float dy2 = dy * dy;

        for (int x = min_x; x <= max_x; ++x) {
            const float dx = x - cx;
            const float dist = std::sqrt(dx * dx + dy2);

            if (dist >= aura_radius) continue;

            // Normalized distance (0 at center, 1 at outer aura boundary)
            const float norm_dist = dist / aura_radius;

            // 1. Core luminosity (bright inner sphere)
            float core_factor = 0.0f;
            if (dist < base_radius) {
                float c = 1.0f - (dist / base_radius);
                core_factor = c * c; // Quadratic smooth falloff
            }

            // 2. Soft outer ethereal aura
            float aura_factor = std::pow(1.0f - norm_dist, 2.5f);

            // Composite alpha and brightness
            float total_alpha = (core_factor * 0.85f + aura_factor * 0.45f);
            total_alpha = std::clamp(total_alpha, 0.0f, 1.0f);

            uint8_t a = static_cast<uint8_t>(total_alpha * 255.0f);
            if (a == 0) continue;

            // Color highlight in center (whitish core blending into emotion tint)
            float core_white_blend = core_factor * 0.75f;
            uint8_t r = static_cast<uint8_t>(std::clamp(color.r * (1.0f - core_white_blend) + 255.0f * core_white_blend, 0.0f, 255.0f));
            uint8_t g = static_cast<uint8_t>(std::clamp(color.g * (1.0f - core_white_blend) + 255.0f * core_white_blend, 0.0f, 255.0f));
            uint8_t b = static_cast<uint8_t>(std::clamp(color.b * (1.0f - core_white_blend) + 255.0f * core_white_blend, 0.0f, 255.0f));

            buffer.set_pixel_straight(x, y, r, g, b, a);
        }
    }

    // Floating harmonic satellite particles (bioluminescent motes)
    for (int i = 0; i < 3; ++i) {
        float angle = t * 0.7f + i * 2.094f; // 120 degrees apart
        float orbit_radius = base_radius * (1.1f + 0.15f * std::sin(t * 1.5f + i));
        float px = cx + std::cos(angle) * orbit_radius;
        float py = cy + std::sin(angle) * orbit_radius;

        int ix = static_cast<int>(px);
        int iy = static_cast<int>(py);
        buffer.blend_pixel(ix, iy, 255, 255, 255, 200);
        buffer.blend_pixel(ix + 1, iy, color.r, color.g, color.b, 120);
        buffer.blend_pixel(ix - 1, iy, color.r, color.g, color.b, 120);
        buffer.blend_pixel(ix, iy + 1, color.r, color.g, color.b, 120);
        buffer.blend_pixel(ix, iy - 1, color.r, color.g, color.b, 120);
    }
}

} // namespace screenmate::renderer

