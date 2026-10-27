#include "cooccurrence.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "parallel.h"

namespace {

void deslocamento(Direction direction, int distance, int* dx, int* dy) {
    switch (direction) {
        case Direction::Right: *dx = distance; *dy = 0; return;
        case Direction::Down: *dx = 0; *dy = distance; return;
        case Direction::DownRight: *dx = distance; *dy = distance; return;
        case Direction::DownLeft: *dx = -distance; *dy = distance; return;
        case Direction::Average: break;
    }
    *dx = distance;
    *dy = 0;
}

constexpr Direction kQuatro[4] = {Direction::Right, Direction::Down, Direction::DownRight,
                                  Direction::DownLeft};

// Contagem de pares que aceita tirar par também, pra janela deslizar sem
// remontar a matriz. Junto da matriz vão acumuladores inteiros que dão cada
// medida sem varrer as L x L células: o histograma de |i - j| (contraste,
// dissimilaridade, homogeneidade), quantas células têm cada contagem
// (uniformidade, entropia, máximo) e as somas das marginais (correlação). Tudo
// inteiro, então tirar e pôr de volta não acumula erro de arredondamento.
struct Contagem {
    int L = 0;
    bool simetrica = false;
    std::vector<int32_t> celula;
    std::vector<int64_t> diferenca;
    std::vector<int64_t> de_contagem;
    int64_t n = 0;
    int64_t si = 0, sj = 0, sii = 0, sjj = 0, sij = 0;
    int32_t maior = 0;

    Contagem(int levels, bool symmetric)
        : L(levels), simetrica(symmetric),
          celula(static_cast<std::size_t>(levels) * levels, 0),
          diferenca(static_cast<std::size_t>(levels), 0), de_contagem(64, 0) {}

    void zera() {
        std::fill(celula.begin(), celula.end(), 0);
        std::fill(diferenca.begin(), diferenca.end(), 0);
        std::fill(de_contagem.begin(), de_contagem.end(), 0);
        n = si = sj = sii = sjj = sij = 0;
        maior = 0;
    }

    void mexe(int i, int j, int delta) {
        int32_t& v = celula[static_cast<std::size_t>(i) * L + j];
        if (v > 0) {
            --de_contagem[static_cast<std::size_t>(v)];
        }
        v += delta;
        if (static_cast<std::size_t>(v) >= de_contagem.size()) {
            de_contagem.resize(static_cast<std::size_t>(v) * 2, 0);
        }
        if (v > 0) {
            ++de_contagem[static_cast<std::size_t>(v)];
        }
        // Na subida o máximo só pode crescer um; na descida ele cai no máximo
        // um, então o laço anda no máximo um passo.
        maior = std::max(maior, v);
        while (maior > 0 && de_contagem[static_cast<std::size_t>(maior)] == 0) {
            --maior;
        }
        diferenca[static_cast<std::size_t>(std::abs(i - j))] += delta;
        n += delta;
        si += static_cast<int64_t>(delta) * i;
        sj += static_cast<int64_t>(delta) * j;
        sii += static_cast<int64_t>(delta) * i * i;
        sjj += static_cast<int64_t>(delta) * j * j;
        sij += static_cast<int64_t>(delta) * i * j;
    }

    void par(int i, int j, int delta) {
        mexe(i, j, delta);
        if (simetrica) {
            mexe(j, i, delta);
        }
    }

    double medida(Haralick m) const {
        if (n <= 0) {
            return 0.0;
        }
        const double N = static_cast<double>(n);
        switch (m) {
            case Haralick::Contrast:
            case Haralick::Dissimilarity:
            case Haralick::Homogeneity: {
                double soma = 0.0;
                for (int k = 0; k < L; ++k) {
                    const double c = static_cast<double>(diferenca[static_cast<std::size_t>(k)]);
                    if (c == 0.0) {
                        continue;
                    }
                    if (m == Haralick::Contrast) {
                        soma += c * k * k;
                    } else if (m == Haralick::Dissimilarity) {
                        soma += c * k;
                    } else {
                        soma += c / (1.0 + static_cast<double>(k) * k);
                    }
                }
                return soma / N;
            }
            case Haralick::Uniformity:
            case Haralick::Energy: {
                double soma = 0.0;
                for (int32_t k = 1; k <= maior; ++k) {
                    soma += static_cast<double>(de_contagem[static_cast<std::size_t>(k)]) * k * k;
                }
                const double u = soma / (N * N);
                return m == Haralick::Uniformity ? u : std::sqrt(u);
            }
            case Haralick::Entropy: {
                // -soma p log p = log N - (1/N) soma c log c, e a soma corre
                // pelas contagens distintas, não pelas células.
                double soma = 0.0;
                for (int32_t k = 2; k <= maior; ++k) {
                    soma += static_cast<double>(de_contagem[static_cast<std::size_t>(k)]) * k *
                            std::log2(static_cast<double>(k));
                }
                return std::log2(N) - soma / N;
            }
            case Haralick::MaxProbability:
                return maior / N;
            case Haralick::Correlation: {
                const long double M = n;
                const long double mi = si / M;
                const long double mj = sj / M;
                const long double vi = sii / M - mi * mi;
                const long double vj = sjj / M - mj * mj;
                const long double di = std::sqrt(std::max<long double>(vi, 0.0L));
                const long double dj = std::sqrt(std::max<long double>(vj, 0.0L));
                if (di < 1e-15L || dj < 1e-15L) {
                    return 1.0;
                }
                return static_cast<double>((sij / M - mi * mj) / (di * dj));
            }
        }
        return 0.0;
    }
};

void conta_tudo(MapView<int32_t> q, int dx, int dy, Contagem& acc) {
    const int y0 = std::max(0, -dy), y1 = std::min(q.height, q.height - dy);
    const int x0 = std::max(0, -dx), x1 = std::min(q.width, q.width - dx);
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            acc.par(q.at(x, y), q.at(x + dx, y + dy), 1);
        }
    }
}

Map<float> mapa_uma_direcao(MapView<int32_t> q, int levels, int dx, int dy, bool symmetric,
                            int radius, Haralick measure) {
    const int W = q.width;
    const int H = q.height;
    Map<float> out(W, H);
    const MapView<float> dst = out.view();

    parallel_for(0, H, [&](int linha0, int linha1) {
        Contagem acc(levels, symmetric);
        for (int y = linha0; y < linha1; ++y) {
            acc.zera();
            const int ya = std::max(0, y - radius);
            const int yb = std::min(H - 1, y + radius);
            // Linhas do primeiro pixel de cada par: ele e o vizinho dentro da
            // janela.
            const int ra = std::max(ya, ya - dy);
            const int rb = std::min(yb, yb - dy);

            const auto coluna = [&](int x, int delta) {
                for (int yy = ra; yy <= rb; ++yy) {
                    acc.par(q.at(x, yy), q.at(x + dx, yy + dy), delta);
                }
            };

            int ca = 0, cb = -1;
            for (int x = 0; x < W; ++x) {
                const int xa = std::max(0, x - radius);
                const int xb = std::min(W - 1, x + radius);
                const int na = std::max(xa, xa - dx);
                const int nb = std::min(xb, xb - dx);

                // Sai o que ficou de fora da faixa nova, entra o que falta
                // nela. Perto da borda a faixa encolhe e cresce, então as duas
                // pontas são tratadas sem supor que ela anda de um em um.
                for (int c = ca; c <= std::min(cb, na - 1); ++c) coluna(c, -1);
                for (int c = std::max(ca, nb + 1); c <= cb; ++c) coluna(c, -1);
                if (cb < ca) {
                    for (int c = na; c <= nb; ++c) coluna(c, 1);
                } else {
                    for (int c = na; c <= std::min(nb, ca - 1); ++c) coluna(c, 1);
                    for (int c = std::max(na, cb + 1); c <= nb; ++c) coluna(c, 1);
                }
                ca = na;
                cb = nb;

                dst.at(x, y) = static_cast<float>(acc.medida(measure));
            }
        }
    });
    return out;
}

}  // namespace

const char* direction_name(Direction direction) {
    switch (direction) {
        case Direction::Right: return "direita";
        case Direction::Down: return "abaixo";
        case Direction::DownRight: return "diagonal descendo";
        case Direction::DownLeft: return "diagonal subindo";
        case Direction::Average: return "média das quatro";
    }
    return "?";
}

const char* haralick_name(Haralick measure) {
    switch (measure) {
        case Haralick::Contrast: return "contraste";
        case Haralick::Dissimilarity: return "dissimilaridade";
        case Haralick::Homogeneity: return "homogeneidade";
        case Haralick::Uniformity: return "uniformidade";
        case Haralick::Energy: return "energia";
        case Haralick::Correlation: return "correlação";
        case Haralick::Entropy: return "entropia";
        case Haralick::MaxProbability: return "probabilidade máxima";
    }
    return "?";
}

Map<int32_t> quantize_levels(MapView<float> scalar, float lo, float hi, int levels) {
    Map<int32_t> out(scalar.width, scalar.height);
    const double faixa = hi > lo ? static_cast<double>(hi) - lo : 1.0;
    for (int y = 0; y < scalar.height; ++y) {
        for (int x = 0; x < scalar.width; ++x) {
            const float v = scalar.at(x, y);
            int nivel = 0;
            if (std::isfinite(v)) {
                nivel = static_cast<int>(std::floor((v - lo) / faixa * levels));
            }
            out.view().at(x, y) = std::clamp(nivel, 0, levels - 1);
        }
    }
    return out;
}

Cooccurrence cooccurrence(MapView<int32_t> quantized, int levels, Direction direction,
                          int distance, bool symmetric) {
    Cooccurrence out;
    out.levels = levels;
    out.p.assign(static_cast<std::size_t>(levels) * levels, 0.0);

    const auto soma_uma = [&](Direction d, double peso) {
        int dx = 0, dy = 0;
        deslocamento(d, distance, &dx, &dy);
        Contagem acc(levels, symmetric);
        conta_tudo(quantized, dx, dy, acc);
        if (acc.n <= 0) {
            return;
        }
        for (std::size_t k = 0; k < out.p.size(); ++k) {
            out.p[k] += peso * acc.celula[k] / static_cast<double>(acc.n);
        }
    };

    if (direction == Direction::Average) {
        for (Direction d : kQuatro) {
            soma_uma(d, 0.25);
        }
    } else {
        soma_uma(direction, 1.0);
    }
    return out;
}

void haralick_of(MapView<int32_t> quantized, int levels, Direction direction, int distance,
                 bool symmetric, double out[haralick_count]) {
    std::fill(out, out + haralick_count, 0.0);
    const auto soma_uma = [&](Direction d, double peso) {
        int dx = 0, dy = 0;
        deslocamento(d, distance, &dx, &dy);
        Contagem acc(levels, symmetric);
        conta_tudo(quantized, dx, dy, acc);
        for (int k = 0; k < haralick_count; ++k) {
            out[k] += peso * acc.medida(static_cast<Haralick>(k));
        }
    };
    if (direction == Direction::Average) {
        for (Direction d : kQuatro) {
            soma_uma(d, 0.25);
        }
    } else {
        soma_uma(direction, 1.0);
    }
}

Map<float> texture_map(MapView<int32_t> quantized, int levels, Direction direction, int distance,
                       bool symmetric, int radius, Haralick measure) {
    if (direction != Direction::Average) {
        int dx = 0, dy = 0;
        deslocamento(direction, distance, &dx, &dy);
        return mapa_uma_direcao(quantized, levels, dx, dy, symmetric, radius, measure);
    }
    Map<float> soma(quantized.width, quantized.height);
    soma.fill(0.0f);
    for (Direction d : kQuatro) {
        int dx = 0, dy = 0;
        deslocamento(d, distance, &dx, &dy);
        const Map<float> uma = mapa_uma_direcao(quantized, levels, dx, dy, symmetric, radius, measure);
        for (std::size_t k = 0; k < soma.count(); ++k) {
            soma.data[k] += 0.25f * uma.data[k];
        }
    }
    return soma;
}
