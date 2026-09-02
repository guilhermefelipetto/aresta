#include "ops.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include "color.h"
#include "expr.h"
#include "parallel.h"

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

const char* channel_name(Channel channel) {
    switch (channel) {
        case Channel::Luma: return "luminância";
        case Channel::Red: return "vermelho";
        case Channel::Green: return "verde";
        case Channel::Blue: return "azul";
        case Channel::Max: return "máximo";
        case Channel::Min: return "mínimo";
        case Channel::Saturation: return "saturação";
    }
    return "?";
}

Map<float> channel_of(ImageView image, Channel channel, const float* weights, bool on_srgb) {
    Map<float> result(image.width, image.height);
    for (int y = 0; y < image.height; ++y) {
        const float* p = image.row(y);
        float* out = result.view().row(y);
        for (int x = 0; x < image.width; ++x, p += 4) {
            float rgb[3] = {p[0], p[1], p[2]};
            if (on_srgb) {
                for (float& c : rgb) {
                    c = linear_to_srgb(std::clamp(c, 0.0f, 1.0f));
                }
            }
            const float hi = std::max({rgb[0], rgb[1], rgb[2]});
            const float lo = std::min({rgb[0], rgb[1], rgb[2]});
            switch (channel) {
                case Channel::Luma:
                    out[x] = weights[0] * rgb[0] + weights[1] * rgb[1] + weights[2] * rgb[2];
                    break;
                case Channel::Red: out[x] = rgb[0]; break;
                case Channel::Green: out[x] = rgb[1]; break;
                case Channel::Blue: out[x] = rgb[2]; break;
                case Channel::Max: out[x] = hi; break;
                case Channel::Min: out[x] = lo; break;
                case Channel::Saturation: out[x] = hi > 1e-6f ? (hi - lo) / hi : 0.0f; break;
            }
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
constexpr int curve_samples = 1024;

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

namespace {

constexpr int clahe_bins = 256;

std::vector<float> clahe_map(const std::vector<float>& values, int width, int height, int tiles,
                             float clip_limit) {
    std::vector<float> out(values.size());
    if (values.empty()) {
        return out;
    }

    tiles = std::clamp(tiles, 1, std::min(width, height));
    float lo = 0.0f;
    float hi = 0.0f;
    scan_range(values.size(), [&](std::size_t i) { return values[i]; }, &lo, &hi);
    const float span = (hi > lo) ? (hi - lo) : 1.0f;

    const auto bin_of = [&](float value) {
        return std::clamp(static_cast<int>((value - lo) / span * clahe_bins), 0, clahe_bins - 1);
    };

    std::vector<std::vector<float>> curves(static_cast<std::size_t>(tiles) * tiles);
    for (int ty = 0; ty < tiles; ++ty) {
        for (int tx = 0; tx < tiles; ++tx) {
            const int x0 = tx * width / tiles;
            const int x1 = (tx + 1) * width / tiles;
            const int y0 = ty * height / tiles;
            const int y1 = (ty + 1) * height / tiles;

            std::vector<double> counts(clahe_bins, 0.0);
            double total = 0.0;
            for (int y = y0; y < y1; ++y) {
                for (int x = x0; x < x1; ++x) {
                    counts[static_cast<std::size_t>(
                        bin_of(values[static_cast<std::size_t>(y) * width + x]))] += 1.0;
                    total += 1.0;
                }
            }
            if (total <= 0.0) {
                curves[static_cast<std::size_t>(ty) * tiles + tx].assign(clahe_bins, 0.0f);
                continue;
            }

            // Corta o que passa do limite e espalha o excedente por igual. Uma
            // rodada resolve quase sempre; a segunda pega o que o espalhamento
            // empurrou de volta pra cima do limite.
            const double limit = std::max(1.0, static_cast<double>(clip_limit) * total / clahe_bins);
            for (int pass = 0; pass < 2; ++pass) {
                double excess = 0.0;
                for (double& count : counts) {
                    if (count > limit) {
                        excess += count - limit;
                        count = limit;
                    }
                }
                if (excess <= 0.0) {
                    break;
                }
                const double share = excess / clahe_bins;
                for (double& count : counts) {
                    count += share;
                }
            }

            std::vector<float>& curve = curves[static_cast<std::size_t>(ty) * tiles + tx];
            curve.resize(clahe_bins);
            double running = 0.0;
            for (int i = 0; i < clahe_bins; ++i) {
                running += counts[static_cast<std::size_t>(i)];
                curve[static_cast<std::size_t>(i)] = static_cast<float>(running / total);
            }
        }
    }

    // Fora do miolo, entre o primeiro e o último centro, não há dois pedaços pra
    // misturar: grampear o índice sem zerar o peso costura o pedaço errado ali e
    // deixa uma emenda visível na borda.
    const auto weights = [tiles](float f, int* i0, int* i1) {
        const int base = static_cast<int>(std::floor(f));
        if (base < 0) {
            *i0 = *i1 = 0;
            return 0.0f;
        }
        if (base >= tiles - 1) {
            *i0 = *i1 = tiles - 1;
            return 0.0f;
        }
        *i0 = base;
        *i1 = base + 1;
        return f - static_cast<float>(base);
    };

    for (int y = 0; y < height; ++y) {
        const float fy = (static_cast<float>(y) + 0.5f) * tiles / height - 0.5f;
        int ty0 = 0;
        int ty1 = 0;
        const float wy = weights(fy, &ty0, &ty1);

        for (int x = 0; x < width; ++x) {
            const float fx = (static_cast<float>(x) + 0.5f) * tiles / width - 0.5f;
            int tx0 = 0;
            int tx1 = 0;
            const float wx = weights(fx, &tx0, &tx1);

            const std::size_t index = static_cast<std::size_t>(y) * width + x;
            const int bin = bin_of(values[index]);
            const auto at = [&](int tx, int ty) {
                return curves[static_cast<std::size_t>(ty) * tiles + tx][static_cast<std::size_t>(
                    bin)];
            };
            const float top = at(tx0, ty0) * (1.0f - wx) + at(tx1, ty0) * wx;
            const float bottom = at(tx0, ty1) * (1.0f - wx) + at(tx1, ty1) * wx;
            out[index] = top * (1.0f - wy) + bottom * wy;
        }
    }
    return out;
}

}  // namespace

void clahe(MapView<float> scalar, int tiles, float clip_limit) {
    std::vector<float> values(static_cast<std::size_t>(scalar.width) * scalar.height);
    std::size_t at = 0;
    for (int y = 0; y < scalar.height; ++y) {
        const float* row = scalar.row(y);
        for (int x = 0; x < scalar.width; ++x) {
            values[at++] = row[x];
        }
    }

    const std::vector<float> mapped = clahe_map(values, scalar.width, scalar.height, tiles,
                                                clip_limit);
    at = 0;
    for (int y = 0; y < scalar.height; ++y) {
        float* row = scalar.row(y);
        for (int x = 0; x < scalar.width; ++x) {
            row[x] = mapped[at++];
        }
    }
}

void clahe(ImageView image, int tiles, float clip_limit) {
    const std::vector<float> values = luma_of(image);
    const std::vector<float> mapped = clahe_map(values, image.width, image.height, tiles,
                                                clip_limit);
    std::size_t at = 0;
    for (int y = 0; y < image.height; ++y) {
        float* p = image.row(y);
        for (int x = 0; x < image.width; ++x, p += 4) {
            retint(p, mapped[at++]);
        }
    }
}

const char* combine_name(Combine operation) {
    switch (operation) {
        case Combine::Add: return "somar";
        case Combine::Subtract: return "subtrair";
        case Combine::AbsDiff: return "diferença absoluta";
        case Combine::Multiply: return "multiplicar";
        case Combine::Divide: return "dividir";
        case Combine::Min: return "mínimo";
        case Combine::Max: return "máximo";
        case Combine::Average: return "média";
    }
    return "?";
}

namespace {

float combine_pair(float a, float b, Combine operation) {
    switch (operation) {
        case Combine::Add: return a + b;
        case Combine::Subtract: return a - b;
        case Combine::AbsDiff: return std::fabs(a - b);
        case Combine::Multiply: return a * b;
        case Combine::Divide: return std::fabs(b) > 1e-6f ? a / b : 0.0f;
        case Combine::Min: return std::min(a, b);
        case Combine::Max: return std::max(a, b);
        case Combine::Average: return (a + b) * 0.5f;
    }
    return a;
}

}  // namespace

void combine(ImageView a, ImageView b, Combine operation, float scale, ImageView out) {
    for (int y = 0; y < out.height; ++y) {
        const float* pa = a.row(std::min(y, a.height - 1));
        const float* pb = b.row(std::min(y, b.height - 1));
        float* q = out.row(y);
        for (int x = 0; x < out.width; ++x) {
            const int xa = std::min(x, a.width - 1) * 4;
            const int xb = std::min(x, b.width - 1) * 4;
            for (int channel = 0; channel < 3; ++channel) {
                q[x * 4 + channel] =
                    combine_pair(pa[xa + channel], pb[xb + channel], operation) * scale;
            }
            q[x * 4 + 3] = pa[xa + 3];
        }
    }
}

void combine(MapView<float> a, MapView<float> b, Combine operation, float scale,
             MapView<float> out) {
    for (int y = 0; y < out.height; ++y) {
        const float* pa = a.row(std::min(y, a.height - 1));
        const float* pb = b.row(std::min(y, b.height - 1));
        float* q = out.row(y);
        for (int x = 0; x < out.width; ++x) {
            q[x] = combine_pair(pa[std::min(x, a.width - 1)], pb[std::min(x, b.width - 1)],
                                operation) *
                   scale;
        }
    }
}

void combine(MapView<int32_t> a, MapView<int32_t> b, Combine operation, float scale,
             MapView<int32_t> out) {
    for (int y = 0; y < out.height; ++y) {
        const int32_t* pa = a.row(std::min(y, a.height - 1));
        const int32_t* pb = b.row(std::min(y, b.height - 1));
        int32_t* q = out.row(y);
        for (int x = 0; x < out.width; ++x) {
            const float value = combine_pair(static_cast<float>(pa[std::min(x, a.width - 1)]),
                                             static_cast<float>(pb[std::min(x, b.width - 1)]),
                                             operation) *
                                scale;
            q[x] = static_cast<int32_t>(std::lround(value));
        }
    }
}

namespace {

bool build_curve(const char* expression, float a, float b, float c, std::vector<float>* table,
                 std::string* error) {
    const Expr parsed = parse_expr(expression ? expression : "");
    if (!parsed.valid()) {
        *error = parsed.error;
        return false;
    }
    error->clear();

    // Amostra a curva uma vez e consulta por índice. Reavaliar a árvore por
    // pixel custaria caro e não daria resolução nenhuma a mais.
    table->resize(curve_samples);
    for (int i = 0; i < curve_samples; ++i) {
        ExprVars vars;
        vars.v = static_cast<float>(i) / (curve_samples - 1);
        vars.a = a;
        vars.b = b;
        vars.c = c;
        (*table)[static_cast<std::size_t>(i)] = parsed.eval(vars);
    }
    return true;
}

float sample_curve(const std::vector<float>& table, float value) {
    const int index = std::clamp(static_cast<int>(value * (curve_samples - 1) + 0.5f), 0,
                                 curve_samples - 1);
    return table[static_cast<std::size_t>(index)];
}

}  // namespace

bool apply_curve(ImageView image, const char* expression, float a, float b, float c, bool on_srgb,
                 std::string* error) {
    std::vector<float> table;
    if (!build_curve(expression, a, b, c, &table, error)) {
        return false;
    }

    for (int y = 0; y < image.height; ++y) {
        float* p = image.row(y);
        for (int x = 0; x < image.width; ++x, p += 4) {
            for (int channel = 0; channel < 3; ++channel) {
                float value = std::clamp(p[channel], 0.0f, 1.0f);
                if (on_srgb) {
                    value = linear_to_srgb(value);
                }
                value = sample_curve(table, value);
                if (on_srgb) {
                    value = srgb_to_linear(std::clamp(value, 0.0f, 1.0f));
                }
                p[channel] = value;
            }
        }
    }
    return true;
}

bool apply_curve(MapView<float> scalar, const char* expression, float a, float b, float c,
                 std::string* error) {
    std::vector<float> table;
    if (!build_curve(expression, a, b, c, &table, error)) {
        return false;
    }

    // Mapa escalar não vive em 0..1 necessariamente; normaliza pelo próprio
    // intervalo pra curva sempre falar a mesma língua.
    float lo = std::numeric_limits<float>::max();
    float hi = std::numeric_limits<float>::lowest();
    for (int y = 0; y < scalar.height; ++y) {
        const float* row = scalar.row(y);
        for (int x = 0; x < scalar.width; ++x) {
            lo = std::min(lo, row[x]);
            hi = std::max(hi, row[x]);
        }
    }
    const float span = (hi > lo) ? (hi - lo) : 1.0f;

    for (int y = 0; y < scalar.height; ++y) {
        float* row = scalar.row(y);
        for (int x = 0; x < scalar.width; ++x) {
            row[x] = sample_curve(table, (row[x] - lo) / span);
        }
    }
    return true;
}

const char* rank_name(Rank kind) {
    switch (kind) {
        case Rank::Median: return "mediana";
        case Rank::Min: return "mínimo";
        case Rank::Max: return "máximo";
        case Rank::Midpoint: return "ponto médio";
        case Rank::AlphaTrimmed: return "média alfa-cortada";
    }
    return "?";
}

namespace {

float rank_of(std::vector<float>& janela, Rank kind, float alpha) {
    const std::size_t n = janela.size();
    switch (kind) {
        case Rank::Median: {
            const std::size_t meio = n / 2;
            std::nth_element(janela.begin(), janela.begin() + meio, janela.end());
            return janela[meio];
        }
        case Rank::Min:
            return *std::min_element(janela.begin(), janela.end());
        case Rank::Max:
            return *std::max_element(janela.begin(), janela.end());
        case Rank::Midpoint:
            return (*std::min_element(janela.begin(), janela.end()) +
                    *std::max_element(janela.begin(), janela.end())) *
                   0.5f;
        default: {
            // Joga fora os `corte` menores e os `corte` maiores e tira a média
            // do que sobra: pega sal e pimenta e ruído gaussiano de uma vez.
            const std::size_t corte =
                std::min(n / 2 - (n % 2 == 0 ? 1 : 0), static_cast<std::size_t>(alpha * n * 0.5f));
            std::sort(janela.begin(), janela.end());
            float soma = 0.0f;
            std::size_t conta = 0;
            for (std::size_t i = corte; i + corte < n; ++i) {
                soma += janela[i];
                ++conta;
            }
            return conta > 0 ? soma / static_cast<float>(conta) : janela[n / 2];
        }
    }
}

}  // namespace

Image rank_filter(ImageView image, const Adjacency& adjacency, Rank kind, float alpha) {
    Image out(image.width, image.height);
    const ImageView dst = out.view();

    parallel_for(0, image.height, [&](int y0, int y1) {
        std::vector<float> janela;
        janela.reserve(adjacency.offsets.size() + 1);
        for (int y = y0; y < y1; ++y) {
            float* row = dst.row(y);
            for (int x = 0; x < image.width; ++x) {
                for (int canal = 0; canal < 3; ++canal) {
                    janela.clear();
                    janela.push_back(image.at(x, y)[canal]);
                    for (const Adjacency::Offset& offset : adjacency.offsets) {
                        const int sx = std::clamp(x + offset.dx, 0, image.width - 1);
                        const int sy = std::clamp(y + offset.dy, 0, image.height - 1);
                        janela.push_back(image.at(sx, sy)[canal]);
                    }
                    row[x * 4 + canal] = rank_of(janela, kind, alpha);
                }
                row[x * 4 + 3] = image.at(x, y)[3];
            }
        }
    });
    return out;
}

Map<float> rank_filter(MapView<float> scalar, const Adjacency& adjacency, Rank kind, float alpha) {
    Map<float> out(scalar.width, scalar.height);
    const MapView<float> dst = out.view();

    parallel_for(0, scalar.height, [&](int y0, int y1) {
        std::vector<float> janela;
        janela.reserve(adjacency.offsets.size() + 1);
        for (int y = y0; y < y1; ++y) {
            float* row = dst.row(y);
            for (int x = 0; x < scalar.width; ++x) {
                janela.clear();
                janela.push_back(scalar.at(x, y));
                for (const Adjacency::Offset& offset : adjacency.offsets) {
                    const int sx = std::clamp(x + offset.dx, 0, scalar.width - 1);
                    const int sy = std::clamp(y + offset.dy, 0, scalar.height - 1);
                    janela.push_back(scalar.at(sx, sy));
                }
                row[x] = rank_of(janela, kind, alpha);
            }
        }
    });
    return out;
}

namespace {

// A acumulada do alvo, invertida: pra cada nível da acumulada da origem, qual
// valor do alvo tem aquela mesma fração de pixels abaixo dele.
struct MatchCurve {
    float lo = 0.0f;
    float span = 1.0f;
    std::vector<float> mapped = std::vector<float>(tone_bins, 0.0f);

    float apply(float value) const {
        const int bin =
            std::clamp(static_cast<int>((value - lo) / span * tone_bins), 0, tone_bins - 1);
        return mapped[static_cast<std::size_t>(bin)];
    }
};

std::vector<double> cdf_of(const std::vector<float>& values, float* lo, float* hi) {
    scan_range(values.size(), [&](std::size_t i) { return values[i]; }, lo, hi);
    const float span = (*hi > *lo) ? (*hi - *lo) : 1.0f;

    std::vector<double> counts(tone_bins, 0.0);
    for (float value : values) {
        counts[static_cast<std::size_t>(
            std::clamp(static_cast<int>((value - *lo) / span * tone_bins), 0, tone_bins - 1))] +=
            1.0;
    }
    double running = 0.0;
    for (double& count : counts) {
        running += count;
        count = running / static_cast<double>(values.size());
    }
    return counts;
}

MatchCurve match_curve(const std::vector<float>& source, const std::vector<float>& reference) {
    MatchCurve curve;
    float hi_source = 0.0f;
    const std::vector<double> cdf_source = cdf_of(source, &curve.lo, &hi_source);
    curve.span = (hi_source > curve.lo) ? (hi_source - curve.lo) : 1.0f;

    float lo_ref = 0.0f;
    float hi_ref = 0.0f;
    const std::vector<double> cdf_ref = cdf_of(reference, &lo_ref, &hi_ref);
    const float span_ref = (hi_ref > lo_ref) ? (hi_ref - lo_ref) : 1.0f;

    // As duas acumuladas são crescentes, então uma varredura casada basta: pra
    // cada faixa da origem, anda no alvo até alcançar a mesma fração.
    int alvo = 0;
    for (int i = 0; i < tone_bins; ++i) {
        while (alvo < tone_bins - 1 && cdf_ref[static_cast<std::size_t>(alvo)] <
                                           cdf_source[static_cast<std::size_t>(i)]) {
            ++alvo;
        }
        curve.mapped[static_cast<std::size_t>(i)] =
            lo_ref + (static_cast<float>(alvo) + 0.5f) / tone_bins * span_ref;
    }
    return curve;
}

std::vector<float> values_of(MapView<float> scalar) {
    std::vector<float> values(static_cast<std::size_t>(scalar.width) * scalar.height);
    std::size_t at = 0;
    for (int y = 0; y < scalar.height; ++y) {
        const float* row = scalar.row(y);
        for (int x = 0; x < scalar.width; ++x) {
            values[at++] = row[x];
        }
    }
    return values;
}

}  // namespace

void match_histogram(MapView<float> scalar, MapView<float> reference) {
    const MatchCurve curve = match_curve(values_of(scalar), values_of(reference));
    for (int y = 0; y < scalar.height; ++y) {
        float* row = scalar.row(y);
        for (int x = 0; x < scalar.width; ++x) {
            row[x] = curve.apply(row[x]);
        }
    }
}

void match_histogram(ImageView image, ImageView reference) {
    const std::vector<float> source = luma_of(image);
    const MatchCurve curve = match_curve(source, luma_of(reference));
    std::size_t at = 0;
    for (int y = 0; y < image.height; ++y) {
        float* p = image.row(y);
        for (int x = 0; x < image.width; ++x, p += 4) {
            retint(p, curve.apply(source[at++]));
        }
    }
}

namespace {

Map<float> plane_of(const std::vector<float>& values, int width, int height, int plane) {
    Map<float> out(width, height);
    float lo = 0.0f;
    float hi = 0.0f;
    scan_range(values.size(), [&](std::size_t i) { return values[i]; }, &lo, &hi);
    const float span = (hi > lo) ? (hi - lo) : 1.0f;

    const int bit = std::clamp(plane, 0, 7);
    for (std::size_t i = 0; i < values.size(); ++i) {
        const int level = std::clamp(static_cast<int>((values[i] - lo) / span * 255.0f + 0.5f), 0,
                                     255);
        out.data[i] = ((level >> bit) & 1) ? 1.0f : 0.0f;
    }
    return out;
}

}  // namespace

Map<float> bit_plane(ImageView image, int plane) {
    // Quantiza sobre o valor codificado, que é onde "o bit k" quer dizer alguma
    // coisa: em linear os níveis não são igualmente espaçados na percepção.
    std::vector<float> values(static_cast<std::size_t>(image.width) * image.height);
    std::size_t at = 0;
    for (int y = 0; y < image.height; ++y) {
        const float* p = image.row(y);
        for (int x = 0; x < image.width; ++x, p += 4) {
            values[at++] = linear_to_srgb(std::clamp(luminance(p[0], p[1], p[2]), 0.0f, 1.0f));
        }
    }
    return plane_of(values, image.width, image.height, plane);
}

Map<float> bit_plane(MapView<float> scalar, int plane) {
    return plane_of(values_of(scalar), scalar.width, scalar.height, plane);
}

Image compose(MapView<float> r, MapView<float> g, MapView<float> b) {
    Image out(r.width, r.height);
    const ImageView dst = out.view();
    for (int y = 0; y < r.height; ++y) {
        float* p = dst.row(y);
        for (int x = 0; x < r.width; ++x, p += 4) {
            p[0] = r.at(x, y);
            p[1] = g.at(std::min(x, g.width - 1), std::min(y, g.height - 1));
            p[2] = b.at(std::min(x, b.width - 1), std::min(y, b.height - 1));
            p[3] = 1.0f;
        }
    }
    return out;
}
