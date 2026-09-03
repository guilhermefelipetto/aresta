#include "fft.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "parallel.h"

namespace {

int next_power_of_two(int n) {
    int p = 1;
    while (p < n) {
        p *= 2;
    }
    return p;
}

void fft_1d(float* re, float* im, int n, bool inverse) {
    for (int i = 1, j = 0; i < n; ++i) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) {
            j ^= bit;
        }
        j ^= bit;
        if (i < j) {
            std::swap(re[i], re[j]);
            std::swap(im[i], im[j]);
        }
    }

    for (int len = 2; len <= n; len <<= 1) {
        const double angle = 2.0 * std::numbers::pi * (inverse ? 1.0 : -1.0) / len;
        const double wr = std::cos(angle);
        const double wi = std::sin(angle);
        for (int i = 0; i < n; i += len) {
            double cr = 1.0;
            double ci = 0.0;
            for (int k = 0; k < len / 2; ++k) {
                const int a = i + k;
                const int b = a + len / 2;
                const double xr = re[b] * cr - im[b] * ci;
                const double xi = re[b] * ci + im[b] * cr;
                re[b] = static_cast<float>(re[a] - xr);
                im[b] = static_cast<float>(im[a] - xi);
                re[a] = static_cast<float>(re[a] + xr);
                im[a] = static_cast<float>(im[a] + xi);
                const double next = cr * wr - ci * wi;
                ci = cr * wi + ci * wr;
                cr = next;
            }
        }
    }

    if (inverse) {
        for (int i = 0; i < n; ++i) {
            re[i] /= static_cast<float>(n);
            im[i] /= static_cast<float>(n);
        }
    }
}

void transform(Spectrum& spectrum, bool inverse) {
    const int w = spectrum.width;
    const int h = spectrum.height;

    parallel_for(0, h, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            fft_1d(spectrum.re.data() + static_cast<std::size_t>(y) * w,
                   spectrum.im.data() + static_cast<std::size_t>(y) * w, w, inverse);
        }
    });

    // Coluna não é contígua, então copia pra um vetor, transforma e devolve.
    parallel_for(0, w, [&](int x0, int x1) {
        std::vector<float> cr(static_cast<std::size_t>(h));
        std::vector<float> ci(static_cast<std::size_t>(h));
        for (int x = x0; x < x1; ++x) {
            for (int y = 0; y < h; ++y) {
                cr[static_cast<std::size_t>(y)] = spectrum.re[static_cast<std::size_t>(y) * w + x];
                ci[static_cast<std::size_t>(y)] = spectrum.im[static_cast<std::size_t>(y) * w + x];
            }
            fft_1d(cr.data(), ci.data(), h, inverse);
            for (int y = 0; y < h; ++y) {
                spectrum.re[static_cast<std::size_t>(y) * w + x] = cr[static_cast<std::size_t>(y)];
                spectrum.im[static_cast<std::size_t>(y) * w + x] = ci[static_cast<std::size_t>(y)];
            }
        }
    });
}

// Frequência com sinal: o índice u acima de N/2 representa frequência
// negativa, e é isso que faz a distância até a origem ser a certa.
float signed_frequency(int index, int size) {
    return static_cast<float>(index <= size / 2 ? index : index - size);
}

}  // namespace

const char* pad_name(Pad pad) {
    return pad == Pad::Mirror ? "espelhar" : "zero";
}

const char* freq_shape_name(FreqShape shape) {
    switch (shape) {
        case FreqShape::Ideal: return "ideal";
        case FreqShape::Butterworth: return "Butterworth";
        case FreqShape::Gaussian: return "gaussiano";
    }
    return "?";
}

const char* freq_kind_name(FreqKind kind) {
    switch (kind) {
        case FreqKind::LowPass: return "passa-baixa";
        case FreqKind::HighPass: return "passa-alta";
        case FreqKind::BandPass: return "passa-faixa";
        case FreqKind::BandReject: return "rejeita-faixa";
    }
    return "?";
}

Spectrum forward_fft(MapView<float> src, Pad pad) {
    Spectrum spectrum;
    if (src.empty()) {
        return spectrum;
    }

    spectrum.source_width = src.width;
    spectrum.source_height = src.height;
    spectrum.width = next_power_of_two(src.width);
    spectrum.height = next_power_of_two(src.height);
    const std::size_t total = static_cast<std::size_t>(spectrum.width) * spectrum.height;
    spectrum.re.assign(total, 0.0f);
    spectrum.im.assign(total, 0.0f);

    for (int y = 0; y < spectrum.height; ++y) {
        for (int x = 0; x < spectrum.width; ++x) {
            float value = 0.0f;
            if (x < src.width && y < src.height) {
                value = src.at(x, y);
            } else if (pad == Pad::Mirror) {
                // Espelha em vez de zerar, senão a borda artificial vira um
                // degrau que enche o espectro de energia que não existe.
                const int mx = x < src.width ? x : std::max(0, 2 * src.width - x - 1);
                const int my = y < src.height ? y : std::max(0, 2 * src.height - y - 1);
                value = src.at(std::clamp(mx, 0, src.width - 1),
                               std::clamp(my, 0, src.height - 1));
            }
            spectrum.re[static_cast<std::size_t>(y) * spectrum.width + x] = value;
        }
    }

    transform(spectrum, false);
    return spectrum;
}

Map<float> inverse_fft(const Spectrum& spectrum) {
    if (spectrum.empty()) {
        return Map<float>{};
    }
    Spectrum copy = spectrum;
    transform(copy, true);

    Map<float> out(spectrum.source_width, spectrum.source_height);
    for (int y = 0; y < out.height; ++y) {
        float* row = out.view().row(y);
        for (int x = 0; x < out.width; ++x) {
            row[x] = copy.re[static_cast<std::size_t>(y) * copy.width + x];
        }
    }
    return out;
}

Map<float> spectrum_magnitude(const Spectrum& spectrum, bool logarithmic) {
    if (spectrum.empty()) {
        return Map<float>{};
    }
    Map<float> out(spectrum.width, spectrum.height);
    const int half_x = spectrum.width / 2;
    const int half_y = spectrum.height / 2;

    for (int y = 0; y < spectrum.height; ++y) {
        for (int x = 0; x < spectrum.width; ++x) {
            const std::size_t at = static_cast<std::size_t>(y) * spectrum.width + x;
            const float value = std::hypot(spectrum.re[at], spectrum.im[at]);
            const int dx = (x + half_x) % spectrum.width;
            const int dy = (y + half_y) % spectrum.height;
            out.view().at(dx, dy) = logarithmic ? std::log1p(value) : value;
        }
    }
    return out;
}

void apply_freq_filter(Spectrum& spectrum, FreqShape shape, FreqKind kind, float cutoff, int order,
                       float width) {
    if (spectrum.empty()) {
        return;
    }

    // Normaliza pelo raio de Nyquist pra o mesmo corte querer dizer a mesma
    // coisa em qualquer resolução.
    const float radius = std::hypot(static_cast<float>(spectrum.width) * 0.5f,
                                    static_cast<float>(spectrum.height) * 0.5f);
    const float d0 = std::max(cutoff, 1e-4f) * radius;
    const float band = std::max(width, 1e-4f) * radius;

    for (int y = 0; y < spectrum.height; ++y) {
        const float fy = signed_frequency(y, spectrum.height);
        for (int x = 0; x < spectrum.width; ++x) {
            const float fx = signed_frequency(x, spectrum.width);
            const float d = std::hypot(fx, fy);

            float gain = 0.0f;
            switch (kind) {
                case FreqKind::LowPass:
                case FreqKind::HighPass: {
                    switch (shape) {
                        case FreqShape::Ideal:
                            gain = d <= d0 ? 1.0f : 0.0f;
                            break;
                        case FreqShape::Butterworth:
                            gain = 1.0f / (1.0f + std::pow(d / d0, 2.0f * order));
                            break;
                        default:
                            gain = std::exp(-(d * d) / (2.0f * d0 * d0));
                            break;
                    }
                    if (kind == FreqKind::HighPass) {
                        gain = 1.0f - gain;
                    }
                    break;
                }
                default: {
                    // Faixa: distância até o anel de raio d0, não até a origem.
                    switch (shape) {
                        case FreqShape::Ideal:
                            gain = std::fabs(d - d0) <= band * 0.5f ? 1.0f : 0.0f;
                            break;
                        case FreqShape::Butterworth: {
                            const float denom = d * d - d0 * d0;
                            const float ratio = std::fabs(denom) < 1e-6f
                                                    ? 1e6f
                                                    : (d * band) / denom;
                            gain = 1.0f / (1.0f + std::pow(1.0f / std::max(ratio, 1e-6f),
                                                           2.0f * order));
                            break;
                        }
                        default: {
                            const float denom = std::max(d * band, 1e-6f);
                            const float e = (d * d - d0 * d0) / denom;
                            gain = std::exp(-0.5f * e * e);
                            break;
                        }
                    }
                    if (kind == FreqKind::BandReject) {
                        gain = 1.0f - gain;
                    }
                    break;
                }
            }

            const std::size_t at = static_cast<std::size_t>(y) * spectrum.width + x;
            spectrum.re[at] *= gain;
            spectrum.im[at] *= gain;
        }
    }
}

Map<float> filter_frequency(MapView<float> scalar, FreqShape shape, FreqKind kind, float cutoff,
                            int order, float width, Pad pad) {
    Spectrum spectrum = forward_fft(scalar, pad);
    apply_freq_filter(spectrum, shape, kind, cutoff, order, width);
    return inverse_fft(spectrum);
}

Image filter_frequency(ImageView image, FreqShape shape, FreqKind kind, float cutoff, int order,
                       float width, Pad pad) {
    Image out(image.width, image.height);
    Map<float> plano(image.width, image.height);

    for (int channel = 0; channel < 3; ++channel) {
        for (int y = 0; y < image.height; ++y) {
            const float* p = image.row(y);
            float* row = plano.view().row(y);
            for (int x = 0; x < image.width; ++x) {
                row[x] = p[x * 4 + channel];
            }
        }
        const Map<float> filtrado =
            filter_frequency(plano.view(), shape, kind, cutoff, order, width, pad);
        for (int y = 0; y < image.height; ++y) {
            const float* row = filtrado.view().row(y);
            float* q = out.view().row(y);
            for (int x = 0; x < image.width; ++x) {
                q[x * 4 + channel] = row[x];
            }
        }
    }
    for (int y = 0; y < image.height; ++y) {
        const float* p = image.row(y);
        float* q = out.view().row(y);
        for (int x = 0; x < image.width; ++x) {
            q[x * 4 + 3] = p[x * 4 + 3];
        }
    }
    return out;
}
