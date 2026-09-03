#include "restore.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <random>
#include <vector>

#include "parallel.h"

const char* noise_name(Noise kind) {
    switch (kind) {
        case Noise::Gaussian: return "gaussiano";
        case Noise::Rayleigh: return "rayleigh";
        case Noise::Gamma: return "gama";
        case Noise::Exponential: return "exponencial";
        case Noise::Uniform: return "uniforme";
        case Noise::SaltPepper: return "sal e pimenta";
        case Noise::Periodic: return "periódico";
    }
    return "?";
}

const char* mean_name(Mean kind) {
    switch (kind) {
        case Mean::Arithmetic: return "aritmética";
        case Mean::Geometric: return "geométrica";
        case Mean::Harmonic: return "harmônica";
        case Mean::Contraharmonic: return "contra-harmônica";
    }
    return "?";
}

namespace {

// Devolve, pra cada pixel, o quanto somar (ou, no caso de sal e pimenta, o
// valor que substitui). Uma amostra por pixel, igual nos três canais, que é
// como o ruído de sensor e o do livro se comportam.
struct NoiseField {
    std::vector<float> value;
    std::vector<unsigned char> replace;  // 1 quando o valor substitui em vez de somar
};

NoiseField build_noise(int width, int height, Noise kind, float a, float b, float c,
                       unsigned seed) {
    const std::size_t total = static_cast<std::size_t>(width) * height;
    NoiseField field;
    field.value.assign(total, 0.0f);
    field.replace.assign(total, 0);

    std::mt19937 rng(seed);
    switch (kind) {
        case Noise::Gaussian: {
            std::normal_distribution<float> dist(a, std::max(b, 0.0f));
            for (float& v : field.value) {
                v = dist(rng);
            }
            break;
        }
        case Noise::Rayleigh: {
            std::uniform_real_distribution<float> u(0.0f, 1.0f);
            for (float& v : field.value) {
                v = a + std::sqrt(-std::max(b, 1e-6f) * std::log(1.0f - u(rng)));
            }
            break;
        }
        case Noise::Gamma: {
            std::gamma_distribution<float> dist(std::max(b, 0.1f), 1.0f / std::max(a, 1e-3f));
            for (float& v : field.value) {
                v = dist(rng);
            }
            break;
        }
        case Noise::Exponential: {
            std::exponential_distribution<float> dist(std::max(a, 1e-3f));
            for (float& v : field.value) {
                v = dist(rng);
            }
            break;
        }
        case Noise::Uniform: {
            std::uniform_real_distribution<float> dist(a, std::max(b, a));
            for (float& v : field.value) {
                v = dist(rng);
            }
            break;
        }
        case Noise::SaltPepper: {
            std::uniform_real_distribution<float> u(0.0f, 1.0f);
            for (std::size_t i = 0; i < total; ++i) {
                const float p = u(rng);
                if (p < a) {
                    field.value[i] = 0.0f;
                    field.replace[i] = 1;
                } else if (p > 1.0f - b) {
                    field.value[i] = 1.0f;
                    field.replace[i] = 1;
                }
            }
            break;
        }
        default: {
            // Duas senoides cruzadas: é o que a notch do capítulo 4 sabe tirar.
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    const float fx = 2.0f * std::numbers::pi_v<float> * b * x / width;
                    const float fy = 2.0f * std::numbers::pi_v<float> * c * y / height;
                    field.value[static_cast<std::size_t>(y) * width + x] =
                        a * (std::sin(fx) + std::sin(fy)) * 0.5f;
                }
            }
            break;
        }
    }
    return field;
}

float rank_window(std::vector<float>& window) {
    const std::size_t middle = window.size() / 2;
    std::nth_element(window.begin(), window.begin() + middle, window.end());
    return window[middle];
}

template <typename Sample>
float mean_at(Sample sample, const Adjacency& adjacency, int x, int y, Mean kind, float q) {
    const float center = sample(x, y);
    double soma = 0.0;
    double soma_log = 0.0;
    double soma_inv = 0.0;
    double soma_num = 0.0;
    double soma_den = 0.0;
    int conta = 0;

    const auto acumula = [&](float v) {
        soma += v;
        soma_log += std::log(std::max(v, 1e-6f));
        soma_inv += 1.0 / std::max(v, 1e-6f);
        soma_num += std::pow(std::max(v, 1e-6f), static_cast<double>(q) + 1.0);
        soma_den += std::pow(std::max(v, 1e-6f), static_cast<double>(q));
        ++conta;
    };

    acumula(center);
    for (const Adjacency::Offset& offset : adjacency.offsets) {
        acumula(sample(x + offset.dx, y + offset.dy));
    }

    switch (kind) {
        case Mean::Arithmetic:
            return static_cast<float>(soma / conta);
        case Mean::Geometric:
            return static_cast<float>(std::exp(soma_log / conta));
        case Mean::Harmonic:
            return static_cast<float>(conta / std::max(soma_inv, 1e-9));
        default:
            return static_cast<float>(soma_num / std::max(soma_den, 1e-9));
    }
}

}  // namespace

void add_noise(MapView<float> scalar, Noise kind, float a, float b, float c, unsigned seed) {
    const NoiseField field = build_noise(scalar.width, scalar.height, kind, a, b, c, seed);
    for (int y = 0; y < scalar.height; ++y) {
        float* row = scalar.row(y);
        for (int x = 0; x < scalar.width; ++x) {
            const std::size_t at = static_cast<std::size_t>(y) * scalar.width + x;
            row[x] = field.replace[at] ? field.value[at] : row[x] + field.value[at];
        }
    }
}

void add_noise(ImageView image, Noise kind, float a, float b, float c, unsigned seed) {
    const NoiseField field = build_noise(image.width, image.height, kind, a, b, c, seed);
    for (int y = 0; y < image.height; ++y) {
        float* p = image.row(y);
        for (int x = 0; x < image.width; ++x, p += 4) {
            const std::size_t at = static_cast<std::size_t>(y) * image.width + x;
            for (int channel = 0; channel < 3; ++channel) {
                p[channel] = field.replace[at] ? field.value[at] : p[channel] + field.value[at];
            }
        }
    }
}

Map<float> mean_filter(MapView<float> scalar, const Adjacency& adjacency, Mean kind, float q) {
    Map<float> out(scalar.width, scalar.height);
    const MapView<float> dst = out.view();
    const auto sample = [&](int x, int y) {
        return scalar.at(std::clamp(x, 0, scalar.width - 1), std::clamp(y, 0, scalar.height - 1));
    };
    parallel_for(0, scalar.height, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            float* row = dst.row(y);
            for (int x = 0; x < scalar.width; ++x) {
                row[x] = mean_at(sample, adjacency, x, y, kind, q);
            }
        }
    });
    return out;
}

Image mean_filter(ImageView image, const Adjacency& adjacency, Mean kind, float q) {
    Image out(image.width, image.height);
    const ImageView dst = out.view();
    parallel_for(0, image.height, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            float* row = dst.row(y);
            for (int x = 0; x < image.width; ++x) {
                for (int channel = 0; channel < 3; ++channel) {
                    const auto sample = [&](int sx, int sy) {
                        return image.at(std::clamp(sx, 0, image.width - 1),
                                        std::clamp(sy, 0, image.height - 1))[channel];
                    };
                    row[x * 4 + channel] = mean_at(sample, adjacency, x, y, kind, q);
                }
                row[x * 4 + 3] = image.at(x, y)[3];
            }
        }
    });
    return out;
}

namespace {

template <typename Sample>
float adaptive_at(Sample sample, const Adjacency& adjacency, int x, int y, float noise_variance) {
    const float center = sample(x, y);
    double soma = center;
    double soma2 = static_cast<double>(center) * center;
    int conta = 1;
    for (const Adjacency::Offset& offset : adjacency.offsets) {
        const float v = sample(x + offset.dx, y + offset.dy);
        soma += v;
        soma2 += static_cast<double>(v) * v;
        ++conta;
    }
    const double media = soma / conta;
    const double variancia = std::max(0.0, soma2 / conta - media * media);

    // A razão passar de 1 quer dizer que a estimativa do ruído está acima da
    // variância local, e aí o certo é devolver a média, não amplificar.
    const double razao = variancia > 1e-12 ? std::min(1.0, noise_variance / variancia) : 1.0;
    return static_cast<float>(center - razao * (center - media));
}

template <typename Sample>
float adaptive_median_at(Sample sample, int x, int y, float max_radius) {
    std::vector<float> window;
    for (float radius = 1.0f; radius <= max_radius + 1e-3f; radius += 0.5f) {
        const Adjacency adjacency = adjacency_by_radius(radius);
        if (adjacency.offsets.empty()) {
            continue;
        }
        window.clear();
        window.push_back(sample(x, y));
        for (const Adjacency::Offset& offset : adjacency.offsets) {
            window.push_back(sample(x + offset.dx, y + offset.dy));
        }
        const float menor = *std::min_element(window.begin(), window.end());
        const float maior = *std::max_element(window.begin(), window.end());
        const float meio = rank_window(window);

        if (meio > menor && meio < maior) {
            const float center = sample(x, y);
            return (center > menor && center < maior) ? center : meio;
        }
    }
    // Nenhuma janela conseguiu; a mediana da maior é o melhor que dá.
    return window.empty() ? sample(x, y) : rank_window(window);
}

}  // namespace

Map<float> adaptive_denoise(MapView<float> scalar, const Adjacency& adjacency,
                            float noise_variance) {
    Map<float> out(scalar.width, scalar.height);
    const MapView<float> dst = out.view();
    const auto sample = [&](int x, int y) {
        return scalar.at(std::clamp(x, 0, scalar.width - 1), std::clamp(y, 0, scalar.height - 1));
    };
    parallel_for(0, scalar.height, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            float* row = dst.row(y);
            for (int x = 0; x < scalar.width; ++x) {
                row[x] = adaptive_at(sample, adjacency, x, y, noise_variance);
            }
        }
    });
    return out;
}

Image adaptive_denoise(ImageView image, const Adjacency& adjacency, float noise_variance) {
    Image out(image.width, image.height);
    const ImageView dst = out.view();
    parallel_for(0, image.height, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            float* row = dst.row(y);
            for (int x = 0; x < image.width; ++x) {
                for (int channel = 0; channel < 3; ++channel) {
                    const auto sample = [&](int sx, int sy) {
                        return image.at(std::clamp(sx, 0, image.width - 1),
                                        std::clamp(sy, 0, image.height - 1))[channel];
                    };
                    row[x * 4 + channel] = adaptive_at(sample, adjacency, x, y, noise_variance);
                }
                row[x * 4 + 3] = image.at(x, y)[3];
            }
        }
    });
    return out;
}

Map<float> adaptive_median(MapView<float> scalar, float max_radius) {
    Map<float> out(scalar.width, scalar.height);
    const MapView<float> dst = out.view();
    const auto sample = [&](int x, int y) {
        return scalar.at(std::clamp(x, 0, scalar.width - 1), std::clamp(y, 0, scalar.height - 1));
    };
    parallel_for(0, scalar.height, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            float* row = dst.row(y);
            for (int x = 0; x < scalar.width; ++x) {
                row[x] = adaptive_median_at(sample, x, y, max_radius);
            }
        }
    });
    return out;
}

Image adaptive_median(ImageView image, float max_radius) {
    Image out(image.width, image.height);
    const ImageView dst = out.view();
    parallel_for(0, image.height, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            float* row = dst.row(y);
            for (int x = 0; x < image.width; ++x) {
                for (int channel = 0; channel < 3; ++channel) {
                    const auto sample = [&](int sx, int sy) {
                        return image.at(std::clamp(sx, 0, image.width - 1),
                                        std::clamp(sy, 0, image.height - 1))[channel];
                    };
                    row[x * 4 + channel] = adaptive_median_at(sample, x, y, max_radius);
                }
                row[x * 4 + 3] = image.at(x, y)[3];
            }
        }
    });
    return out;
}
