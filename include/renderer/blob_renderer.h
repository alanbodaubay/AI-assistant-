#pragma once

#include "renderer/bgra_buffer.h"
#include <cmath>
#include <string>

namespace screenmate::renderer {

enum class BlobEmotion {
    Calm,      // Cyan / Azure (Default idle)
    Focused,   // Emerald / Teal (Observing screen)
    Witty,     // Gold / Amber (Bantering / amused)
    Thinking,  // Violet / Amethyst (Inferring)
    Alert      // Coral / Rose
};

struct BlobColor {
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

class BlobRenderer {
public:
    BlobRenderer(int dimension = 128);

    /**
     * @brief Renders one frame of the organic bioluminescent blob into the buffer.
     * @param buffer Output 32-bit premultiplied BGRA buffer.
     * @param time_seconds Elapsed continuous time in seconds.
     * @param emotion Current emotional color state.
     * @param activity_level 0.0 (quiet idle) to 1.0 (active speaking/thinking).
     */
    void render(BgraBuffer& buffer, double time_seconds, BlobEmotion emotion = BlobEmotion::Calm, float activity_level = 0.0f);

    void set_dimension(int dimension);
    int dimension() const noexcept { return dimension_; }

private:
    int dimension_;
    BlobColor get_emotion_color(BlobEmotion emotion) const noexcept;
};

} // namespace screenmate::renderer
