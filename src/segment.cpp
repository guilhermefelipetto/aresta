#include "segment.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <queue>
#include <utility>
#include <vector>
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

Map<float> hough_accumulator(MapView<int32_t> edges, int thetas, int rhos) {
    thetas = std::clamp(thetas, 16, 1024);
    rhos = std::clamp(rhos, 16, 2048);

    const float diagonal =
        std::hypot(static_cast<float>(edges.width), static_cast<float>(edges.height));
    Map<float> out(thetas, rhos);
    out.fill(0.0f);

    std::vector<float> seno(static_cast<std::size_t>(thetas));
    std::vector<float> cosseno(static_cast<std::size_t>(thetas));
    for (int t = 0; t < thetas; ++t) {
        const float angulo = std::numbers::pi_v<float> * t / thetas;
        seno[static_cast<std::size_t>(t)] = std::sin(angulo);
        cosseno[static_cast<std::size_t>(t)] = std::cos(angulo);
    }

    for (int y = 0; y < edges.height; ++y) {
        for (int x = 0; x < edges.width; ++x) {
            if (edges.at(x, y) == 0) {
                continue;
            }
            for (int t = 0; t < thetas; ++t) {
                const float rho = x * cosseno[static_cast<std::size_t>(t)] +
                                  y * seno[static_cast<std::size_t>(t)];
                const int linha = static_cast<int>((rho + diagonal) / (2.0f * diagonal) * rhos);
                if (linha >= 0 && linha < rhos) {
                    out.view().at(t, linha) += 1.0f;
                }
            }
        }
    }
    return out;
}

namespace {

// Picos que dominam a vizinhança no acumulador, do mais votado pro menos.
std::vector<std::pair<int, int>> peaks(const Map<float>& acc, float threshold, int limit,
                                       int raio) {
    float maior = 0.0f;
    for (std::size_t i = 0; i < acc.count(); ++i) {
        maior = std::max(maior, acc.data[i]);
    }
    const float corte = threshold * maior;

    std::vector<std::pair<float, int>> candidatos;
    for (int y = 0; y < acc.height; ++y) {
        for (int x = 0; x < acc.width; ++x) {
            const float v = acc.view().at(x, y);
            if (v < corte || v <= 0.0f) {
                continue;
            }
            bool domina = true;
            for (int dy = -raio; dy <= raio && domina; ++dy) {
                for (int dx = -raio; dx <= raio && domina; ++dx) {
                    const int sx = x + dx;
                    const int sy = y + dy;
                    if (sx < 0 || sy < 0 || sx >= acc.width || sy >= acc.height) {
                        continue;
                    }
                    domina = acc.view().at(sx, sy) <= v;
                }
            }
            if (domina) {
                candidatos.push_back({v, y * acc.width + x});
            }
        }
    }

    std::sort(candidatos.rbegin(), candidatos.rend());
    std::vector<std::pair<int, int>> saida;
    for (const auto& [valor, at] : candidatos) {
        if (static_cast<int>(saida.size()) >= limit) {
            break;
        }
        saida.push_back({at % acc.width, at / acc.width});
    }
    return saida;
}

void draw_line(MapView<int32_t> out, float rho, float angulo) {
    const float c = std::cos(angulo);
    const float s = std::sin(angulo);
    const int passos = 2 * (out.width + out.height);
    for (int i = -passos; i <= passos; ++i) {
        const float t = static_cast<float>(i) * 0.5f;
        const int x = static_cast<int>(std::lround(rho * c - t * s));
        const int y = static_cast<int>(std::lround(rho * s + t * c));
        if (x >= 0 && y >= 0 && x < out.width && y < out.height) {
            out.at(x, y) = 1;
        }
    }
}

}  // namespace

Map<int32_t> hough_lines(MapView<int32_t> edges, int thetas, int rhos, float threshold,
                         int max_lines) {
    const Map<float> acc = hough_accumulator(edges, thetas, rhos);
    const float diagonal =
        std::hypot(static_cast<float>(edges.width), static_cast<float>(edges.height));

    Map<int32_t> out(edges.width, edges.height);
    out.fill(0);
    for (const auto& [t, linha] : peaks(acc, threshold, std::max(max_lines, 1), 3)) {
        const float angulo = std::numbers::pi_v<float> * t / acc.width;
        const float rho = (static_cast<float>(linha) + 0.5f) / acc.height * 2.0f * diagonal -
                          diagonal;
        draw_line(out.view(), rho, angulo);
    }
    return out;
}

Map<int32_t> hough_circles(MapView<int32_t> edges, float min_radius, float max_radius, float step,
                           float threshold, int max_circles) {
    min_radius = std::max(min_radius, 1.0f);
    max_radius = std::max(max_radius, min_radius);
    step = std::max(step, 0.5f);

    Map<int32_t> out(edges.width, edges.height);
    out.fill(0);

    struct Achado {
        float votos;
        int x;
        int y;
        float raio;
    };
    std::vector<Achado> achados;

    Map<float> acc(edges.width, edges.height);
    for (float raio = min_radius; raio <= max_radius + 1e-3f; raio += step) {
        acc.fill(0.0f);
        const int amostras = std::max(16, static_cast<int>(2.0f * std::numbers::pi_v<float> * raio));
        for (int y = 0; y < edges.height; ++y) {
            for (int x = 0; x < edges.width; ++x) {
                if (edges.at(x, y) == 0) {
                    continue;
                }
                for (int k = 0; k < amostras; ++k) {
                    const float a = 2.0f * std::numbers::pi_v<float> * k / amostras;
                    const int cx = x - static_cast<int>(std::lround(raio * std::cos(a)));
                    const int cy = y - static_cast<int>(std::lround(raio * std::sin(a)));
                    if (cx >= 0 && cy >= 0 && cx < edges.width && cy < edges.height) {
                        acc.view().at(cx, cy) += 1.0f;
                    }
                }
            }
        }
        // Normaliza pelo perímetro, senão raio grande sempre ganha.
        for (std::size_t i = 0; i < acc.count(); ++i) {
            acc.data[i] /= static_cast<float>(amostras);
        }
        for (const auto& [cx, cy] : peaks(acc, threshold, max_circles, 4)) {
            achados.push_back({acc.view().at(cx, cy), cx, cy, raio});
        }
    }

    std::sort(achados.begin(), achados.end(),
              [](const Achado& a, const Achado& b) { return a.votos > b.votos; });
    if (static_cast<int>(achados.size()) > max_circles) {
        achados.resize(static_cast<std::size_t>(std::max(max_circles, 0)));
    }

    for (const Achado& achado : achados) {
        const int amostras = std::max(64, static_cast<int>(8.0f * achado.raio));
        for (int k = 0; k < amostras; ++k) {
            const float a = 2.0f * std::numbers::pi_v<float> * k / amostras;
            const int x = achado.x + static_cast<int>(std::lround(achado.raio * std::cos(a)));
            const int y = achado.y + static_cast<int>(std::lround(achado.raio * std::sin(a)));
            if (x >= 0 && y >= 0 && x < out.width && y < out.height) {
                out.view().at(x, y) = 1;
            }
        }
    }
    return out;
}

Map<int32_t> watershed(MapView<float> relief, MapView<int32_t> markers, MapView<int32_t> mask,
                       const Adjacency& adjacency, bool draw_lines) {
    constexpr int32_t divisor = -1;

    Map<int32_t> out(relief.width, relief.height);
    out.fill(0);

    struct Frente {
        float custo;
        long long ordem;
        int at;
        bool operator<(const Frente& outro) const {
            // priority_queue tira o maior, então a comparação vai invertida. A
            // ordem de entrada desempata, e isso importa: sem ela o resultado
            // muda conforme a implementação da fila.
            return custo > outro.custo || (custo == outro.custo && ordem > outro.ordem);
        }
    };

    std::priority_queue<Frente> fila;
    long long ordem = 0;
    std::vector<unsigned char> na_fila(out.count(), 0);

    const auto dentro = [&](int x, int y) {
        return mask.empty() || mask.at(std::min(x, mask.width - 1),
                                       std::min(y, mask.height - 1)) != 0;
    };

    const auto empilhar_vizinhos = [&](int x, int y) {
        for (const Adjacency::Offset& offset : adjacency.offsets) {
            const int qx = x + offset.dx;
            const int qy = y + offset.dy;
            if (qx < 0 || qy < 0 || qx >= relief.width || qy >= relief.height) {
                continue;
            }
            const std::size_t at = static_cast<std::size_t>(qy) * relief.width + qx;
            if (out.view().at(qx, qy) == 0 && !na_fila[at] && dentro(qx, qy)) {
                na_fila[at] = 1;
                fila.push({relief.at(qx, qy), ordem++, static_cast<int>(at)});
            }
        }
    };

    for (int y = 0; y < relief.height; ++y) {
        for (int x = 0; x < relief.width; ++x) {
            const int32_t m = markers.at(std::min(x, markers.width - 1),
                                         std::min(y, markers.height - 1));
            if (m > 0) {
                out.view().at(x, y) = m;
            }
        }
    }
    for (int y = 0; y < relief.height; ++y) {
        for (int x = 0; x < relief.width; ++x) {
            if (out.view().at(x, y) > 0) {
                empilhar_vizinhos(x, y);
            }
        }
    }

    // Decidir na hora de tirar da fila, e não na de botar: é isso que faz o
    // divisor sair com um pixel em vez de cobrir toda a região de encontro.
    while (!fila.empty()) {
        const Frente atual = fila.top();
        fila.pop();
        const int x = atual.at % relief.width;
        const int y = atual.at / relief.width;
        if (out.view().at(x, y) != 0) {
            continue;
        }

        int32_t escolhido = 0;
        bool conflito = false;
        for (const Adjacency::Offset& offset : adjacency.offsets) {
            const int qx = x + offset.dx;
            const int qy = y + offset.dy;
            if (qx < 0 || qy < 0 || qx >= relief.width || qy >= relief.height) {
                continue;
            }
            const int32_t vizinho = out.view().at(qx, qy);
            if (vizinho <= 0) {
                continue;
            }
            if (escolhido == 0) {
                escolhido = vizinho;
            } else if (escolhido != vizinho) {
                conflito = true;
                break;
            }
        }

        if (conflito && draw_lines) {
            out.view().at(x, y) = divisor;
            continue;
        }
        if (escolhido == 0) {
            continue;
        }
        out.view().at(x, y) = escolhido;
        empilhar_vizinhos(x, y);
    }

    for (std::size_t i = 0; i < out.count(); ++i) {
        if (out.data[i] == divisor) {
            out.data[i] = 0;
        }
    }
    return out;
}
