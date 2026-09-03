#include "segment.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <numbers>

#include "convolve.h"
#include "kernel.h"
#include "parallel.h"

namespace {

int odd_size(float sigma) {
    const int lado = static_cast<int>(std::ceil(sigma * 3.0f)) * 2 + 1;
    return std::clamp(lado, 3, 31);
}

void range_of(MapView<float> m, float* lo, float* hi) {
    *lo = std::numeric_limits<float>::max();
    *hi = std::numeric_limits<float>::lowest();
    for (int y = 0; y < m.height; ++y) {
        const float* row = m.row(y);
        for (int x = 0; x < m.width; ++x) {
            *lo = std::min(*lo, row[x]);
            *hi = std::max(*hi, row[x]);
        }
    }
}

}  // namespace

Map<int32_t> canny(MapView<float> scalar, float sigma, float low, float high) {
    if (low > high) {
        std::swap(low, high);
    }

    const Map<float> suave =
        sigma > 0.05f ? convolve(scalar, kernel_gaussian(odd_size(sigma), sigma), Border::Clamp,
                                 false)
                      : Map<float>{};
    const MapView<float> base = suave.empty() ? scalar : suave.view();

    const Kernel sx = KernelLibrary().find("sobel x")->kernel;
    const Kernel sy = KernelLibrary().find("sobel y")->kernel;
    const Map<float> gx = convolve(base, sx, Border::Clamp, false);
    const Map<float> gy = convolve(base, sy, Border::Clamp, false);

    Map<float> magnitude(scalar.width, scalar.height);
    float pico = 0.0f;
    for (int y = 0; y < scalar.height; ++y) {
        for (int x = 0; x < scalar.width; ++x) {
            const float m = std::hypot(gx.view().at(x, y), gy.view().at(x, y));
            magnitude.view().at(x, y) = m;
            pico = std::max(pico, m);
        }
    }
    if (pico <= 0.0f) {
        Map<int32_t> vazio(scalar.width, scalar.height);
        vazio.fill(0);
        return vazio;
    }

    // Afinar comparando com os dois vizinhos na direção do gradiente. Sem isso
    // a borda sai com a largura do kernel, e não com um pixel.
    Map<float> afinado(scalar.width, scalar.height);
    afinado.fill(0.0f);
    for (int y = 1; y < scalar.height - 1; ++y) {
        for (int x = 1; x < scalar.width - 1; ++x) {
            const float dx = gx.view().at(x, y);
            const float dy = gy.view().at(x, y);
            float angulo = std::atan2(dy, dx) * 180.0f / std::numbers::pi_v<float>;
            if (angulo < 0.0f) {
                angulo += 180.0f;
            }

            int ax = 1;
            int ay = 0;
            if (angulo >= 22.5f && angulo < 67.5f) {
                ax = 1;
                ay = -1;
            } else if (angulo >= 67.5f && angulo < 112.5f) {
                ax = 0;
                ay = 1;
            } else if (angulo >= 112.5f && angulo < 157.5f) {
                ax = 1;
                ay = 1;
            }

            const float m = magnitude.view().at(x, y);
            if (m >= magnitude.view().at(x + ax, y + ay) &&
                m >= magnitude.view().at(x - ax, y - ay)) {
                afinado.view().at(x, y) = m;
            }
        }
    }

    const float corte_alto = high * pico;
    const float corte_baixo = low * pico;

    Map<int32_t> out(scalar.width, scalar.height);
    out.fill(0);
    std::vector<int> pending;
    for (int y = 0; y < scalar.height; ++y) {
        for (int x = 0; x < scalar.width; ++x) {
            if (afinado.view().at(x, y) >= corte_alto) {
                out.view().at(x, y) = 1;
                pending.push_back(y * scalar.width + x);
            }
        }
    }

    // Histerese: o fraco só entra se estiver ligado a um forte.
    while (!pending.empty()) {
        const int at = pending.back();
        pending.pop_back();
        const int px = at % scalar.width;
        const int py = at / scalar.width;
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                const int qx = px + dx;
                const int qy = py + dy;
                if (qx < 0 || qy < 0 || qx >= scalar.width || qy >= scalar.height) {
                    continue;
                }
                if (out.view().at(qx, qy) == 0 && afinado.view().at(qx, qy) >= corte_baixo) {
                    out.view().at(qx, qy) = 1;
                    pending.push_back(qy * scalar.width + qx);
                }
            }
        }
    }
    return out;
}

Map<int32_t> log_zero_crossings(MapView<float> scalar, float sigma, float slope) {
    const Map<float> resposta =
        convolve(scalar, kernel_log(odd_size(sigma), sigma), Border::Clamp, false);

    float lo = 0.0f;
    float hi = 0.0f;
    range_of(resposta.view(), &lo, &hi);
    const float escala = std::max(std::fabs(lo), std::fabs(hi));
    const float corte = slope * std::max(escala, 1e-6f);

    Map<int32_t> out(scalar.width, scalar.height);
    out.fill(0);
    for (int y = 1; y < scalar.height - 1; ++y) {
        for (int x = 1; x < scalar.width - 1; ++x) {
            const float centro = resposta.view().at(x, y);
            bool cruzou = false;
            for (int k = 0; k < 4 && !cruzou; ++k) {
                const int dx = k == 0 ? 1 : (k == 1 ? 0 : (k == 2 ? 1 : 1));
                const int dy = k == 0 ? 0 : (k == 1 ? 1 : (k == 2 ? 1 : -1));
                const float a = resposta.view().at(x - dx, y - dy);
                const float b = resposta.view().at(x + dx, y + dy);
                // Troca de sinal entre os vizinhos opostos, com salto grande o
                // bastante pra não ser ruído de região lisa.
                cruzou = (a * b < 0.0f) && (std::fabs(a - b) > corte);
            }
            if (cruzou && std::fabs(centro) <= std::max(std::fabs(lo), std::fabs(hi))) {
                out.view().at(x, y) = 1;
            }
        }
    }
    return out;
}

const char* local_threshold_name(LocalThreshold kind) {
    switch (kind) {
        case LocalThreshold::Mean: return "média";
        case LocalThreshold::Gaussian: return "gaussiana";
        case LocalThreshold::Sauvola: return "Sauvola";
    }
    return "?";
}

Map<int32_t> adaptive_threshold(MapView<float> scalar, LocalThreshold kind, float radius,
                                float offset, float k) {
    const int lado = std::clamp(static_cast<int>(radius) * 2 + 1, 3, 61);
    const Kernel janela = kind == LocalThreshold::Gaussian
                              ? kernel_gaussian(lado, std::max(radius * 0.5f, 0.5f))
                              : kernel_box(lado, lado);

    const Map<float> media = convolve(scalar, janela, Border::Clamp, false);

    Map<float> quadrados(scalar.width, scalar.height);
    if (kind == LocalThreshold::Sauvola) {
        for (int y = 0; y < scalar.height; ++y) {
            const float* row = scalar.row(y);
            float* q = quadrados.view().row(y);
            for (int x = 0; x < scalar.width; ++x) {
                q[x] = row[x] * row[x];
            }
        }
        quadrados = convolve(quadrados.view(), janela, Border::Clamp, false);
    }

    float lo = 0.0f;
    float hi = 0.0f;
    range_of(scalar, &lo, &hi);
    const float amplitude = std::max(hi - lo, 1e-6f);

    Map<int32_t> out(scalar.width, scalar.height);
    for (int y = 0; y < scalar.height; ++y) {
        const float* row = scalar.row(y);
        int32_t* q = out.view().row(y);
        for (int x = 0; x < scalar.width; ++x) {
            const float m = media.view().at(x, y);
            float limiar = m - offset;
            if (kind == LocalThreshold::Sauvola) {
                const float variancia = std::max(0.0f, quadrados.view().at(x, y) - m * m);
                const float desvio = std::sqrt(variancia);
                limiar = m * (1.0f + k * (desvio / (amplitude * 0.5f) - 1.0f));
            }
            q[x] = row[x] > limiar ? 1 : 0;
        }
    }
    return out;
}

Map<int32_t> multi_otsu(MapView<float> scalar, int classes, std::vector<float>* levels) {
    constexpr int bins = 256;
    classes = std::clamp(classes, 2, 4);

    float lo = 0.0f;
    float hi = 0.0f;
    range_of(scalar, &lo, &hi);
    const float span = (hi > lo) ? (hi - lo) : 1.0f;

    std::vector<double> conta(bins, 0.0);
    double total = 0.0;
    for (int y = 0; y < scalar.height; ++y) {
        const float* row = scalar.row(y);
        for (int x = 0; x < scalar.width; ++x) {
            ++conta[static_cast<std::size_t>(
                std::clamp(static_cast<int>((row[x] - lo) / span * bins), 0, bins - 1))];
            ++total;
        }
    }

    // Tabelas do Liao: peso e soma acumulados deixam a variância entre classes
    // sair em tempo constante pra qualquer intervalo.
    std::vector<double> peso(bins + 1, 0.0);
    std::vector<double> soma(bins + 1, 0.0);
    for (int i = 0; i < bins; ++i) {
        peso[static_cast<std::size_t>(i + 1)] = peso[static_cast<std::size_t>(i)] + conta[static_cast<std::size_t>(i)];
        soma[static_cast<std::size_t>(i + 1)] =
            soma[static_cast<std::size_t>(i)] + static_cast<double>(i) * conta[static_cast<std::size_t>(i)];
    }
    const auto termo = [&](int a, int b) {
        const double w = peso[static_cast<std::size_t>(b)] - peso[static_cast<std::size_t>(a)];
        if (w <= 0.0) {
            return 0.0;
        }
        const double s = soma[static_cast<std::size_t>(b)] - soma[static_cast<std::size_t>(a)];
        return s * s / w;
    };

    const int cortes = classes - 1;
    std::vector<int> melhor(static_cast<std::size_t>(cortes), 0);
    double melhor_valor = -1.0;
    std::vector<int> atual(static_cast<std::size_t>(cortes), 0);

    const std::function<void(int, int)> busca = [&](int nivel, int inicio) {
        if (nivel == cortes) {
            double valor = 0.0;
            int anterior = 0;
            for (int i = 0; i < cortes; ++i) {
                valor += termo(anterior, atual[static_cast<std::size_t>(i)]);
                anterior = atual[static_cast<std::size_t>(i)];
            }
            valor += termo(anterior, bins);
            if (valor > melhor_valor) {
                melhor_valor = valor;
                melhor = atual;
            }
            return;
        }
        for (int t = inicio; t < bins - (cortes - nivel - 1); ++t) {
            atual[static_cast<std::size_t>(nivel)] = t;
            busca(nivel + 1, t + 1);
        }
    };
    busca(0, 1);

    // Num vazio entre dois picos, qualquer corte separa igual e a busca devolve
    // o primeiro. Centralizar deixa o número reportado querer dizer algo.
    for (int& t : melhor) {
        int a = t;
        while (a > 1 && conta[static_cast<std::size_t>(a - 1)] == 0.0) {
            --a;
        }
        int b = t;
        while (b < bins - 1 && conta[static_cast<std::size_t>(b)] == 0.0) {
            ++b;
        }
        t = (a + b) / 2;
    }

    if (levels) {
        levels->clear();
        for (int t : melhor) {
            levels->push_back(lo + static_cast<float>(t) / bins * span);
        }
    }

    Map<int32_t> out(scalar.width, scalar.height);
    for (int y = 0; y < scalar.height; ++y) {
        const float* row = scalar.row(y);
        int32_t* q = out.view().row(y);
        for (int x = 0; x < scalar.width; ++x) {
            const int bin = std::clamp(static_cast<int>((row[x] - lo) / span * bins), 0, bins - 1);
            int classe = 0;
            for (int t : melhor) {
                if (bin >= t) {
                    ++classe;
                }
            }
            q[x] = classe;
        }
    }
    return out;
}
