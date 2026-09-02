#include "ops.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

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

namespace {

constexpr int tone_bins = 4096;

struct ToneCurve {
    float lo = 0.0f;
    float span = 1.0f;
    std::vector<float> mapped = std::vector<float>(tone_bins, 0.0f);

    float apply(float value) const {
        const int bin = std::clamp(static_cast<int>((value - lo) / span * tone_bins), 0,
                                   tone_bins - 1);
        return mapped[static_cast<std::size_t>(bin)];
    }
};

template <typename Reader>
void scan_range(std::size_t count, Reader read, float* lo, float* hi) {
    *lo = std::numeric_limits<float>::max();
    *hi = std::numeric_limits<float>::lowest();
    for (std::size_t i = 0; i < count; ++i) {
        const float v = read(i);
        *lo = std::min(*lo, v);
        *hi = std::max(*hi, v);
    }
}

template <typename Reader>
ToneCurve equalization_curve(std::size_t count, Reader read) {
    ToneCurve curve;
    float hi = 0.0f;
    scan_range(count, read, &curve.lo, &hi);
    curve.span = (hi > curve.lo) ? (hi - curve.lo) : 1.0f;

    std::vector<double> counts(tone_bins, 0.0);
    for (std::size_t i = 0; i < count; ++i) {
        const int bin = std::clamp(
            static_cast<int>((read(i) - curve.lo) / curve.span * tone_bins), 0, tone_bins - 1);
        counts[static_cast<std::size_t>(bin)] += 1.0;
    }

    // A acumulada normalizada é a própria curva: o valor novo de um pixel é a
    // fração da imagem que estava abaixo dele.
    double running = 0.0;
    const double total = static_cast<double>(count);
    for (int i = 0; i < tone_bins; ++i) {
        running += counts[static_cast<std::size_t>(i)];
        curve.mapped[static_cast<std::size_t>(i)] = static_cast<float>(running / total);
    }
    return curve;
}

template <typename Reader>
ToneCurve stretch_curve(std::size_t count, Reader read, float low_percentile,
                        float high_percentile) {
    ToneCurve curve;
    float hi = 0.0f;
    scan_range(count, read, &curve.lo, &hi);
    curve.span = (hi > curve.lo) ? (hi - curve.lo) : 1.0f;

    std::vector<double> counts(tone_bins, 0.0);
    for (std::size_t i = 0; i < count; ++i) {
        const int bin = std::clamp(
            static_cast<int>((read(i) - curve.lo) / curve.span * tone_bins), 0, tone_bins - 1);
        counts[static_cast<std::size_t>(bin)] += 1.0;
    }

    const double total = static_cast<double>(count);
    const double want_low = total * std::clamp(low_percentile, 0.0f, 100.0f) / 100.0;
    const double want_high = total * std::clamp(high_percentile, 0.0f, 100.0f) / 100.0;

    double running = 0.0;
    int bin_low = 0;
    int bin_high = tone_bins - 1;
    bool found_low = false;
    for (int i = 0; i < tone_bins; ++i) {
        running += counts[static_cast<std::size_t>(i)];
        if (!found_low && running >= want_low) {
            bin_low = i;
            found_low = true;
        }
        if (running >= want_high) {
            bin_high = i;
            break;
        }
    }
    if (bin_high <= bin_low) {
        bin_high = std::min(tone_bins - 1, bin_low + 1);
    }

    const float value_low = curve.lo + static_cast<float>(bin_low) / tone_bins * curve.span;
    const float value_high = curve.lo + static_cast<float>(bin_high) / tone_bins * curve.span;
    const float width = value_high - value_low;
    for (int i = 0; i < tone_bins; ++i) {
        const float value = curve.lo + static_cast<float>(i) / tone_bins * curve.span;
        curve.mapped[static_cast<std::size_t>(i)] =
            std::clamp((value - value_low) / width, 0.0f, 1.0f);
    }
    return curve;
}

// Reescala o RGB pela razão entre a luminância nova e a velha. Mexer canal a
// canal desloca a cor; isso mantém a proporção entre eles.
void retint(float* pixel, float target) {
    const float before = luminance(pixel[0], pixel[1], pixel[2]);
    if (before <= 1e-6f) {
        pixel[0] = pixel[1] = pixel[2] = target;
        return;
    }
    const float factor = target / before;
    pixel[0] *= factor;
    pixel[1] *= factor;
    pixel[2] *= factor;
}

std::vector<float> luma_of(ImageView image) {
    std::vector<float> values(static_cast<std::size_t>(image.width) * image.height);
    std::size_t at = 0;
    for (int y = 0; y < image.height; ++y) {
        const float* p = image.row(y);
        for (int x = 0; x < image.width; ++x, p += 4) {
            values[at++] = luminance(p[0], p[1], p[2]);
        }
    }
    return values;
}

template <typename MakeCurve>
void apply_to_scalar(MapView<float> scalar, MakeCurve make_curve) {
    const std::size_t count = static_cast<std::size_t>(scalar.width) * scalar.height;
    std::vector<float> values(count);
    std::size_t at = 0;
    for (int y = 0; y < scalar.height; ++y) {
        const float* row = scalar.row(y);
        for (int x = 0; x < scalar.width; ++x) {
            values[at++] = row[x];
        }
    }
    const ToneCurve curve = make_curve(count, [&](std::size_t i) { return values[i]; });
    for (int y = 0; y < scalar.height; ++y) {
        float* row = scalar.row(y);
        for (int x = 0; x < scalar.width; ++x) {
            row[x] = curve.apply(row[x]);
        }
    }
}

template <typename MakeCurve>
void apply_to_image(ImageView image, MakeCurve make_curve) {
    const std::vector<float> values = luma_of(image);
    const ToneCurve curve = make_curve(values.size(), [&](std::size_t i) { return values[i]; });
    std::size_t at = 0;
    for (int y = 0; y < image.height; ++y) {
        float* p = image.row(y);
        for (int x = 0; x < image.width; ++x, p += 4) {
            retint(p, curve.apply(values[at++]));
        }
    }
}

}  // namespace

void equalize(MapView<float> scalar) {
    apply_to_scalar(scalar, [](std::size_t n, auto read) { return equalization_curve(n, read); });
}

void equalize(ImageView image) {
    apply_to_image(image, [](std::size_t n, auto read) { return equalization_curve(n, read); });
}

void stretch(MapView<float> scalar, float low_percentile, float high_percentile) {
    apply_to_scalar(scalar, [&](std::size_t n, auto read) {
        return stretch_curve(n, read, low_percentile, high_percentile);
    });
}

void stretch(ImageView image, float low_percentile, float high_percentile) {
    apply_to_image(image, [&](std::size_t n, auto read) {
        return stretch_curve(n, read, low_percentile, high_percentile);
    });
}
