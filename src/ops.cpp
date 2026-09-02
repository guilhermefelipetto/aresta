#include "ops.h"

#include <algorithm>
#include <cmath>

#include "color.h"

namespace {

// Cinza médio em linear. Contraste pivotando em 0.5 só faria sentido se o
// buffer fosse sRGB.
constexpr float mid_gray = 0.18f;

}  // namespace

void adjust_exposure(ImageView image, float stops) {
    if (stops == 0.0f) {
        return;
    }
    const float factor = std::pow(2.0f, stops);
    for (int y = 0; y < image.height; ++y) {
        float* p = image.row(y);
        for (int x = 0; x < image.width; ++x, p += 4) {
            p[0] *= factor;
            p[1] *= factor;
            p[2] *= factor;
        }
    }
}

void adjust_contrast(ImageView image, float amount) {
    if (amount == 0.0f) {
        return;
    }
    const float factor = 1.0f + amount;
    for (int y = 0; y < image.height; ++y) {
        float* p = image.row(y);
        for (int x = 0; x < image.width; ++x, p += 4) {
            p[0] = (p[0] - mid_gray) * factor + mid_gray;
            p[1] = (p[1] - mid_gray) * factor + mid_gray;
            p[2] = (p[2] - mid_gray) * factor + mid_gray;
        }
    }
}

void adjust_gamma(ImageView image, float gamma) {
    if (gamma == 1.0f) {
        return;
    }
    const float inv = 1.0f / gamma;
    for (int y = 0; y < image.height; ++y) {
        float* p = image.row(y);
        for (int x = 0; x < image.width; ++x, p += 4) {
            p[0] = std::pow(std::max(p[0], 0.0f), inv);
            p[1] = std::pow(std::max(p[1], 0.0f), inv);
            p[2] = std::pow(std::max(p[2], 0.0f), inv);
        }
    }
}

void invert(ImageView image) {
    for (int y = 0; y < image.height; ++y) {
        float* p = image.row(y);
        for (int x = 0; x < image.width; ++x, p += 4) {
            p[0] = 1.0f - p[0];
            p[1] = 1.0f - p[1];
            p[2] = 1.0f - p[2];
        }
    }
}

Map<float> luminance_of(ImageView image) {
    Map<float> result(image.width, image.height);
    for (int y = 0; y < image.height; ++y) {
        const float* p = image.row(y);
        float* out = result.view().row(y);
        for (int x = 0; x < image.width; ++x, p += 4) {
            out[x] = luminance(p[0], p[1], p[2]);
        }
    }
    return result;
}

Map<int32_t> threshold(MapView<float> scalar, float level) {
    Map<int32_t> result(scalar.width, scalar.height);
    for (int y = 0; y < scalar.height; ++y) {
        const float* p = scalar.row(y);
        int32_t* out = result.view().row(y);
        for (int x = 0; x < scalar.width; ++x) {
            out[x] = p[x] > level ? 1 : 0;
        }
    }
    return result;
}

void overlay_labels(ImageView image, MapView<int32_t> labels, float opacity) {
    const float alpha = std::clamp(opacity, 0.0f, 1.0f);
    for (int y = 0; y < image.height && y < labels.height; ++y) {
        float* p = image.row(y);
        const int32_t* l = labels.row(y);
        for (int x = 0; x < image.width && x < labels.width; ++x, p += 4) {
            if (l[x] <= 0) {
                continue;
            }
            // Mesma rotação de matiz que o visualizador de rótulo usa, pra
            // sobreposição e visualização direta não discordarem de cor.
            const float hue = std::fmod(static_cast<float>(l[x]) * 0.61803399f, 1.0f) * 6.0f;
            const int sector = static_cast<int>(hue);
            const float f = hue - static_cast<float>(sector);
            const float v = 0.95f;
            const float q = v * (1.0f - 0.75f * f);
            const float t = v * (1.0f - 0.75f * (1.0f - f));
            float rgb[3] = {0.25f, 0.25f, 0.25f};
            switch (sector) {
                case 0: rgb[0] = v; rgb[1] = t; break;
                case 1: rgb[0] = q; rgb[1] = v; break;
                case 2: rgb[1] = v; rgb[2] = t; break;
                case 3: rgb[1] = q; rgb[2] = v; break;
                case 4: rgb[0] = t; rgb[2] = v; break;
                default: rgb[0] = v; rgb[2] = q; break;
            }
            for (int c = 0; c < 3; ++c) {
                p[c] = p[c] * (1.0f - alpha) + srgb_to_linear(rgb[c]) * alpha;
            }
        }
    }
}
