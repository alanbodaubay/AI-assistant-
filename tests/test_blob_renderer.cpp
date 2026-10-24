#include "renderer/blob_renderer.h"
#include "renderer/bgra_buffer.h"
#include <iostream>
#include <cassert>

int main() {
    std::cout << "[TEST] Starting BlobRenderer and BgraBuffer unit tests...\n";

    // 1. Test Buffer Allocation
    screenmate::renderer::BgraBuffer buffer(128, 128);
    assert(buffer.width() == 128);
    assert(buffer.height() == 128);
    assert(buffer.stride() == 128 * 4);
    assert(buffer.size_bytes() == 128 * 128 * 4);

    // 2. Test Blob Rendering
    screenmate::renderer::BlobRenderer renderer(128);
    renderer.render(buffer, 1.0, screenmate::renderer::BlobEmotion::Calm, 0.0f);

    // Center pixel should be luminous and non-zero
    const uint8_t* pixels = buffer.data();
    size_t center_idx = (64 * 128 + 64) * 4;
    uint8_t center_b = pixels[center_idx + 0];
    uint8_t center_g = pixels[center_idx + 1];
    uint8_t center_r = pixels[center_idx + 2];
    uint8_t center_a = pixels[center_idx + 3];

    std::cout << "Center pixel (BGRA): " 
              << (int)center_b << ", " << (int)center_g << ", " 
              << (int)center_r << ", " << (int)center_a << "\n";

    assert(center_a > 100); // Core must be visible
    assert(center_b <= center_a); // Premultiplied invariant
    assert(center_g <= center_a);
    assert(center_r <= center_a);

    // Boundary pixel (corner 0,0) must be completely transparent
    if (pixels[3] != 0) {
        std::cerr << "Error: corner alpha is non-zero: " << (int)pixels[3] << "\n";
        return 1;
    }

    // 3. Test Emotion Transitions
    for (int e = 0; e <= 4; ++e) {
        auto emotion = static_cast<screenmate::renderer::BlobEmotion>(e);
        renderer.render(buffer, 2.5, emotion, 0.5f);
        assert(pixels[center_idx + 3] > 0);
    }

    std::cout << "[TEST] All BlobRenderer unit tests PASSED successfully!\n";
    return 0;
}
