#include "histogram.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "color.h"

namespace {

void tally(std::vector<float>& counts, int channel, int bins, float value, float lo, float span) {
    const int bin = std::clamp(static_cast<int>((value - lo) / span * static_cast<float>(bins)), 0,
                               bins - 1);
    counts[static_cast<std::size_t>(channel) * bins + bin] += 1.0f;
}

}  // namespace

Histogram histogram_of(const Value& value, int bins, bool srgb) {
    Histogram result;
    if (value.empty() || bins < 2) {
        return result;
    }

    result.bins = bins;
    result.channels = (value.kind == ValueKind::Color) ? 4 : 1;
    result.counts.assign(static_cast<std::size_t>(result.channels) * bins, 0.0f);

    const std::size_t total =
        static_cast<std::size_t>(value.width()) * static_cast<std::size_t>(value.height());

    if (value.kind == ValueKind::Color) {
        result.lo = 0.0f;
        result.hi = 1.0f;
        const float span = 1.0f;
        const float* p = value.color.data.get();
        for (std::size_t i = 0; i < total; ++i, p += 4) {
            float rgb[3] = {p[0], p[1], p[2]};
            if (srgb) {
                for (float& c : rgb) {
                    c = linear_to_srgb(std::clamp(c, 0.0f, 1.0f));
                }
            }
            for (int c = 0; c < 3; ++c) {
                tally(result.counts, c, bins, rgb[c], 0.0f, span);
            }
            tally(result.counts, 3, bins, luminance(rgb[0], rgb[1], rgb[2]), 0.0f, span);
        }
    } else {
        float lo = std::numeric_limits<float>::max();
        float hi = std::numeric_limits<float>::lowest();
        const auto visit = [&](auto reader) {
            for (std::size_t i = 0; i < total; ++i) {
                const float v = reader(i);
                lo = std::min(lo, v);
                hi = std::max(hi, v);
            }
        };
        if (value.kind == ValueKind::Scalar) {
            const float* p = value.scalar.data.get();
            visit([&](std::size_t i) { return p[i]; });
        } else {
            const int32_t* p = value.label.data.get();
            visit([&](std::size_t i) { return static_cast<float>(p[i]); });
        }

        result.lo = lo;
        result.hi = hi;
        const float span = (hi > lo) ? (hi - lo) : 1.0f;
        if (value.kind == ValueKind::Scalar) {
            const float* p = value.scalar.data.get();
            for (std::size_t i = 0; i < total; ++i) {
                tally(result.counts, 0, bins, p[i], lo, span);
            }
        } else {
            const int32_t* p = value.label.data.get();
            for (std::size_t i = 0; i < total; ++i) {
                tally(result.counts, 0, bins, static_cast<float>(p[i]), lo, span);
            }
        }
    }

    for (float count : result.counts) {
        result.peak = std::max(result.peak, count);
    }
    return result;
}

float otsu_threshold(MapView<float> scalar) {
    constexpr int bins = 256;

    float lo = std::numeric_limits<float>::max();
    float hi = std::numeric_limits<float>::lowest();
    for (int y = 0; y < scalar.height; ++y) {
        const float* row = scalar.row(y);
        for (int x = 0; x < scalar.width; ++x) {
            lo = std::min(lo, row[x]);
            hi = std::max(hi, row[x]);
        }
    }
    if (!(hi > lo)) {
        return lo;
    }
    const float span = hi - lo;

    double count[bins] = {};
    double total = 0.0;
    for (int y = 0; y < scalar.height; ++y) {
        const float* row = scalar.row(y);
        for (int x = 0; x < scalar.width; ++x) {
            const int bin =
                std::clamp(static_cast<int>((row[x] - lo) / span * bins), 0, bins - 1);
            count[bin] += 1.0;
            total += 1.0;
        }
    }

    double sum_all = 0.0;
    for (int i = 0; i < bins; ++i) {
        sum_all += static_cast<double>(i) * count[i];
    }

    // Maximiza a variância entre as duas classes, que é o mesmo que minimizar a
    // variância dentro delas mas custa uma passada só.
    double weight_low = 0.0;
    double sum_low = 0.0;
    double best_variance = -1.0;
    int best_bin = 0;
    for (int i = 0; i < bins; ++i) {
        weight_low += count[i];
        if (weight_low == 0.0) {
            continue;
        }
        const double weight_high = total - weight_low;
        if (weight_high == 0.0) {
            break;
        }
        sum_low += static_cast<double>(i) * count[i];
        const double mean_low = sum_low / weight_low;
        const double mean_high = (sum_all - sum_low) / weight_high;
        const double delta = mean_low - mean_high;
        const double variance = weight_low * weight_high * delta * delta;
        if (variance > best_variance) {
            best_variance = variance;
            best_bin = i;
        }
    }

    return lo + (static_cast<float>(best_bin) + 0.5f) / bins * span;
}
