#include "metrics.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "color.h"

namespace {

// Janela do SSIM: gaussiana 11x11 com sigma 1.5, que é a do artigo do Wang.
// Trocar por caixa muda o número o bastante pra não comparar com paper.
constexpr int kRaio = 5;
constexpr float kSigma = 1.5f;

std::vector<float> janela() {
    std::vector<float> w(2 * kRaio + 1);
    float soma = 0.0f;
    for (int i = -kRaio; i <= kRaio; ++i) {
        w[i + kRaio] = std::exp(-(i * i) / (2.0f * kSigma * kSigma));
        soma += w[i + kRaio];
    }
    for (float& v : w) {
        v /= soma;
    }
    return w;
}

// Separável, com a borda estendida. Estender é o que menos inventa estrutura
// onde não tem, e estrutura é justamente o que o SSIM está medindo.
Map<float> borra(MapView<float> origem, const std::vector<float>& w) {
    Map<float> meio(origem.width, origem.height);
    for (int y = 0; y < origem.height; ++y) {
        for (int x = 0; x < origem.width; ++x) {
            float soma = 0.0f;
            for (int i = -kRaio; i <= kRaio; ++i) {
                const int sx = std::clamp(x + i, 0, origem.width - 1);
                soma += origem.at(sx, y) * w[i + kRaio];
            }
            meio.view().at(x, y) = soma;
        }
    }
    Map<float> saida(origem.width, origem.height);
    for (int y = 0; y < origem.height; ++y) {
        for (int x = 0; x < origem.width; ++x) {
            float soma = 0.0f;
            for (int i = -kRaio; i <= kRaio; ++i) {
                const int sy = std::clamp(y + i, 0, origem.height - 1);
                soma += meio.view().at(x, sy) * w[i + kRaio];
            }
            saida.view().at(x, y) = soma;
        }
    }
    return saida;
}

Map<float> produto(MapView<float> a, MapView<float> b) {
    Map<float> saida(a.width, a.height);
    for (int y = 0; y < a.height; ++y) {
        for (int x = 0; x < a.width; ++x) {
            saida.view().at(x, y) = a.at(x, y) * b.at(x, y);
        }
    }
    return saida;
}

float pico_valido(float peak) { return peak > 0.0f ? peak : 1.0f; }

Map<float> canal(ImageView imagem, int c, bool on_srgb) {
    Map<float> saida(imagem.width, imagem.height);
    for (int y = 0; y < imagem.height; ++y) {
        for (int x = 0; x < imagem.width; ++x) {
            const float v = imagem.at(x, y)[c];
            saida.view().at(x, y) = on_srgb ? linear_to_srgb(v) : v;
        }
    }
    return saida;
}

Map<float> ssim_bruto(MapView<float> a, MapView<float> b, float peak) {
    const float L = pico_valido(peak);
    const float c1 = (0.01f * L) * (0.01f * L);
    const float c2 = (0.03f * L) * (0.03f * L);
    const std::vector<float> w = janela();

    const Map<float> mu_a = borra(a, w);
    const Map<float> mu_b = borra(b, w);
    const Map<float> aa = produto(a, a);
    const Map<float> bb = produto(b, b);
    const Map<float> ab = produto(a, b);
    const Map<float> saa = borra(aa.view(), w);
    const Map<float> sbb = borra(bb.view(), w);
    const Map<float> sab = borra(ab.view(), w);

    Map<float> saida(a.width, a.height);
    for (int y = 0; y < a.height; ++y) {
        for (int x = 0; x < a.width; ++x) {
            const float ma = mu_a.view().at(x, y);
            const float mb = mu_b.view().at(x, y);
            const float va = saa.view().at(x, y) - ma * ma;
            const float vb = sbb.view().at(x, y) - mb * mb;
            const float vab = sab.view().at(x, y) - ma * mb;
            const float cima = (2.0f * ma * mb + c1) * (2.0f * vab + c2);
            const float baixo = (ma * ma + mb * mb + c1) * (va + vb + c2);
            saida.view().at(x, y) = baixo != 0.0f ? cima / baixo : 1.0f;
        }
    }
    return saida;
}

// A média do SSIM ignora a faixa da borda, onde a janela saiu da imagem e o
// valor diz mais sobre o preenchimento que sobre a imagem. É o que o código do
// artigo faz e o que o scikit-image faz, então o número dá pra comparar.
double media_do_miolo(MapView<float> mapa) {
    const int x0 = std::min(kRaio, mapa.width / 2);
    const int y0 = std::min(kRaio, mapa.height / 2);
    const int x1 = std::max(x0 + 1, mapa.width - x0);
    const int y1 = std::max(y0 + 1, mapa.height - y0);

    double soma = 0.0;
    long long n = 0;
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            soma += mapa.at(x, y);
            ++n;
        }
    }
    return n > 0 ? soma / static_cast<double>(n) : 0.0;
}

}  // namespace

float peak_of(MapView<float> map) {
    float lo = map.at(0, 0);
    float hi = lo;
    for (int y = 0; y < map.height; ++y) {
        for (int x = 0; x < map.width; ++x) {
            lo = std::min(lo, map.at(x, y));
            hi = std::max(hi, map.at(x, y));
        }
    }
    return hi > lo ? hi - lo : 1.0f;
}

float peak_of(ImageView image, bool on_srgb) {
    float lo = 1e30f;
    float hi = -1e30f;
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            for (int c = 0; c < 3; ++c) {
                const float v = image.at(x, y)[c];
                const float u = on_srgb ? linear_to_srgb(v) : v;
                lo = std::min(lo, u);
                hi = std::max(hi, u);
            }
        }
    }
    return hi > lo ? hi - lo : 1.0f;
}

Metrics compare(MapView<float> reference, MapView<float> measured, float peak) {
    Metrics m;
    m.peak = pico_valido(peak);
    if (reference.width != measured.width || reference.height != measured.height
        || reference.width <= 0) {
        return m;
    }

    double soma_abs = 0.0;
    double soma_quad = 0.0;
    double soma_sinal = 0.0;
    for (int y = 0; y < reference.height; ++y) {
        for (int x = 0; x < reference.width; ++x) {
            const double r = reference.at(x, y);
            const double d = r - measured.at(x, y);
            soma_abs += std::abs(d);
            soma_quad += d * d;
            soma_sinal += r * r;
        }
    }
    const double n = static_cast<double>(reference.width) * reference.height;
    m.mae = soma_abs / n;
    m.mse = soma_quad / n;
    m.rmse = std::sqrt(m.mse);

    // Erro zero daria log de zero. Infinito é a resposta certa, e quem mostra
    // decide como escrever isso.
    m.psnr_db = m.mse > 0.0 ? 10.0 * std::log10(m.peak * m.peak / m.mse) : INFINITY;
    m.snr_db = (m.mse > 0.0 && soma_sinal > 0.0)
                   ? 10.0 * std::log10(soma_sinal / soma_quad)
                   : INFINITY;

    const Map<float> mapa = ssim_bruto(reference, measured, m.peak);
    m.ssim = media_do_miolo(mapa.view());
    return m;
}

Metrics compare(ImageView reference, ImageView measured, float peak, bool on_srgb) {
    Metrics m;
    m.peak = pico_valido(peak);
    if (reference.width != measured.width || reference.height != measured.height
        || reference.width <= 0) {
        return m;
    }

    double mae = 0.0;
    double mse = 0.0;
    double snr = 0.0;
    double ssim = 0.0;
    bool snr_infinito = false;
    for (int c = 0; c < 3; ++c) {
        const Map<float> a = canal(reference, c, on_srgb);
        const Map<float> b = canal(measured, c, on_srgb);
        const Metrics um = compare(a.view(), b.view(), m.peak);
        mae += um.mae;
        mse += um.mse;
        ssim += um.ssim;
        if (std::isinf(um.snr_db)) {
            snr_infinito = true;
        } else {
            snr += um.snr_db;
        }
    }
    m.mae = mae / 3.0;
    m.mse = mse / 3.0;
    m.rmse = std::sqrt(m.mse);
    m.ssim = ssim / 3.0;

    // O PSNR sai do erro médio dos três canais, não da média dos três PSNR:
    // média de logaritmo não é o logaritmo da média.
    m.psnr_db = m.mse > 0.0 ? 10.0 * std::log10(m.peak * m.peak / m.mse) : INFINITY;
    m.snr_db = snr_infinito ? INFINITY : snr / 3.0;
    return m;
}

Map<float> ssim_map(MapView<float> reference, MapView<float> measured, float peak) {
    if (reference.width != measured.width || reference.height != measured.height) {
        return Map<float>(std::max(reference.width, 1), std::max(reference.height, 1));
    }
    return ssim_bruto(reference, measured, pico_valido(peak));
}

Map<float> ssim_map(ImageView reference, ImageView measured, float peak, bool on_srgb) {
    Map<float> saida(reference.width, reference.height);
    if (reference.width != measured.width || reference.height != measured.height) {
        return saida;
    }
    for (std::size_t i = 0; i < saida.count(); ++i) {
        saida.data[i] = 0.0f;
    }
    for (int c = 0; c < 3; ++c) {
        const Map<float> a = canal(reference, c, on_srgb);
        const Map<float> b = canal(measured, c, on_srgb);
        const Map<float> um = ssim_bruto(a.view(), b.view(), pico_valido(peak));
        for (std::size_t i = 0; i < saida.count(); ++i) {
            saida.data[i] += um.data[i] / 3.0f;
        }
    }
    return saida;
}

const char* metric_map_name(MetricMap kind) {
    switch (kind) {
        case MetricMap::AbsError: return "erro absoluto";
        case MetricMap::SquaredError: return "erro ao quadrado";
        case MetricMap::Ssim: return "mapa SSIM";
    }
    return "?";
}

Map<float> metric_map(MapView<float> reference, MapView<float> measured, MetricMap kind,
                      float peak) {
    if (kind == MetricMap::Ssim) {
        return ssim_map(reference, measured, peak);
    }
    Map<float> saida(reference.width, reference.height);
    for (int y = 0; y < reference.height; ++y) {
        for (int x = 0; x < reference.width; ++x) {
            const float d = reference.at(x, y) - measured.at(x, y);
            saida.view().at(x, y) = kind == MetricMap::SquaredError ? d * d : std::abs(d);
        }
    }
    return saida;
}

Map<float> metric_map(ImageView reference, ImageView measured, MetricMap kind, float peak,
                      bool on_srgb) {
    if (kind == MetricMap::Ssim) {
        return ssim_map(reference, measured, peak, on_srgb);
    }
    Map<float> saida(reference.width, reference.height);
    for (int y = 0; y < reference.height; ++y) {
        for (int x = 0; x < reference.width; ++x) {
            float soma = 0.0f;
            for (int c = 0; c < 3; ++c) {
                const float a = reference.at(x, y)[c];
                const float b = measured.at(x, y)[c];
                const float d = on_srgb ? (linear_to_srgb(a) - linear_to_srgb(b)) : (a - b);
                soma += kind == MetricMap::SquaredError ? d * d : std::abs(d);
            }
            saida.view().at(x, y) = soma / 3.0f;
        }
    }
    return saida;
}
