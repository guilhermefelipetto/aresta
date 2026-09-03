#include "morphology.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "parallel.h"

Adjacency adjacency_by_radius(float radius) {
    Adjacency adjacency;
    adjacency.radius = std::max(radius, 0.0f);
    const int reach = static_cast<int>(std::floor(adjacency.radius));
    const float limit = adjacency.radius * adjacency.radius;
    for (int dy = -reach; dy <= reach; ++dy) {
        for (int dx = -reach; dx <= reach; ++dx) {
            if (dx == 0 && dy == 0) {
                continue;
            }
            if (static_cast<float>(dx * dx + dy * dy) <= limit) {
                adjacency.offsets.push_back({dx, dy});
            }
        }
    }
    return adjacency;
}

const char* morph_name(Morph operation) {
    switch (operation) {
        case Morph::Erode: return "erosão";
        case Morph::Dilate: return "dilatação";
        case Morph::Open: return "abertura";
        case Morph::Close: return "fechamento";
        case Morph::Gradient: return "gradiente";
        case Morph::TopHat: return "top-hat";
        case Morph::BlackHat: return "black-hat";
    }
    return "?";
}

namespace {

template <typename T>
Map<T> rank_filter(MapView<T> src, const Adjacency& adjacency, bool maximum) {
    Map<T> out(src.width, src.height);
    const MapView<T> dst = out.view();

    parallel_for(0, src.height, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            T* row = dst.row(y);
            for (int x = 0; x < src.width; ++x) {
                T best = src.at(x, y);
                for (const Adjacency::Offset& offset : adjacency.offsets) {
                    const int sx = std::clamp(x + offset.dx, 0, src.width - 1);
                    const int sy = std::clamp(y + offset.dy, 0, src.height - 1);
                    const T value = src.at(sx, sy);
                    if (maximum ? (value > best) : (value < best)) {
                        best = value;
                    }
                }
                row[x] = best;
            }
        }
    });
    return out;
}

template <typename T>
Map<T> combine(MapView<T> a, MapView<T> b, bool subtract) {
    Map<T> out(a.width, a.height);
    const MapView<T> dst = out.view();
    for (int y = 0; y < a.height; ++y) {
        const T* pa = a.row(y);
        const T* pb = b.row(y);
        T* q = dst.row(y);
        for (int x = 0; x < a.width; ++x) {
            q[x] = subtract ? static_cast<T>(pa[x] - pb[x]) : pa[x];
        }
    }
    return out;
}

template <typename T>
Map<T> run(MapView<T> src, const Adjacency& adjacency, Morph operation) {
    switch (operation) {
        case Morph::Erode:
            return rank_filter(src, adjacency, false);
        case Morph::Dilate:
            return rank_filter(src, adjacency, true);
        case Morph::Open: {
            Map<T> eroded = rank_filter(src, adjacency, false);
            return rank_filter(eroded.view(), adjacency, true);
        }
        case Morph::Close: {
            Map<T> dilated = rank_filter(src, adjacency, true);
            return rank_filter(dilated.view(), adjacency, false);
        }
        case Morph::Gradient: {
            Map<T> dilated = rank_filter(src, adjacency, true);
            Map<T> eroded = rank_filter(src, adjacency, false);
            return combine<T>(dilated.view(), eroded.view(), true);
        }
        case Morph::TopHat: {
            Map<T> eroded = rank_filter(src, adjacency, false);
            Map<T> opened = rank_filter(eroded.view(), adjacency, true);
            return combine<T>(src, opened.view(), true);
        }
        case Morph::BlackHat: {
            Map<T> dilated = rank_filter(src, adjacency, true);
            Map<T> closed = rank_filter(dilated.view(), adjacency, false);
            return combine<T>(closed.view(), src, true);
        }
    }
    return Map<T>(src.width, src.height);
}

}  // namespace

Map<float> morphology(MapView<float> src, const Adjacency& adjacency, Morph operation) {
    return run<float>(src, adjacency, operation);
}

Map<int32_t> morphology(MapView<int32_t> src, const Adjacency& adjacency, Morph operation) {
    return run<int32_t>(src, adjacency, operation);
}

Map<int32_t> connected_components(MapView<int32_t> src, const Adjacency& adjacency, int* count) {
    Map<int32_t> out(src.width, src.height);
    out.fill(0);
    const MapView<int32_t> dst = out.view();

    // Pilha explícita: uma componente pode cobrir a imagem inteira e recursão
    // aqui estoura o stack em imagem grande.
    std::vector<int> pending;
    int next_label = 0;

    for (int y = 0; y < src.height; ++y) {
        for (int x = 0; x < src.width; ++x) {
            if (src.at(x, y) == 0 || dst.at(x, y) != 0) {
                continue;
            }

            const int32_t wanted = src.at(x, y);
            ++next_label;
            dst.at(x, y) = next_label;
            pending.push_back(y * src.width + x);

            while (!pending.empty()) {
                const int index = pending.back();
                pending.pop_back();
                const int px = index % src.width;
                const int py = index / src.width;

                for (const Adjacency::Offset& offset : adjacency.offsets) {
                    const int qx = px + offset.dx;
                    const int qy = py + offset.dy;
                    if (qx < 0 || qy < 0 || qx >= src.width || qy >= src.height) {
                        continue;
                    }
                    if (src.at(qx, qy) != wanted || dst.at(qx, qy) != 0) {
                        continue;
                    }
                    dst.at(qx, qy) = next_label;
                    pending.push_back(qy * src.width + qx);
                }
            }
        }
    }

    if (count) {
        *count = next_label;
    }
    return out;
}

namespace {

constexpr float infinito = 1e20f;

// Envoltória inferior de parábolas: o miolo do algoritmo separável.
void distance_1d(const float* f, float* d, int n, int* v, float* z) {
    int k = 0;
    v[0] = 0;
    z[0] = -infinito;
    z[1] = infinito;

    for (int q = 1; q < n; ++q) {
        float s = ((f[q] + static_cast<float>(q) * q) -
                   (f[v[k]] + static_cast<float>(v[k]) * v[k])) /
                  (2.0f * static_cast<float>(q - v[k]));
        while (s <= z[k]) {
            --k;
            s = ((f[q] + static_cast<float>(q) * q) -
                 (f[v[k]] + static_cast<float>(v[k]) * v[k])) /
                (2.0f * static_cast<float>(q - v[k]));
        }
        ++k;
        v[k] = q;
        z[k] = s;
        z[k + 1] = infinito;
    }

    k = 0;
    for (int q = 0; q < n; ++q) {
        while (z[k + 1] < static_cast<float>(q)) {
            ++k;
        }
        const float dq = static_cast<float>(q - v[k]);
        d[q] = dq * dq + f[v[k]];
    }
}

}  // namespace

const char* metric_name(Metric metric) {
    switch (metric) {
        case Metric::Euclidean: return "euclidiana";
        case Metric::CityBlock: return "D4";
        case Metric::Chessboard: return "D8";
    }
    return "?";
}

namespace {

// D4 e D8 são exatas com duas varreduras de chanfro, porque os passos delas são
// inteiros e a menor rota já é soma de passos.
Map<float> chamfer(MapView<int32_t> labels, bool inside, Metric metric) {
    Map<float> out(labels.width, labels.height);
    const MapView<float> dst = out.view();
    const float diagonal = metric == Metric::CityBlock ? infinito : 1.0f;

    for (int y = 0; y < labels.height; ++y) {
        for (int x = 0; x < labels.width; ++x) {
            const bool semente = inside ? (labels.at(x, y) == 0) : (labels.at(x, y) != 0);
            dst.at(x, y) = semente ? 0.0f : infinito;
        }
    }

    const auto relaxa = [&](int x, int y, int dx, int dy, float peso) {
        const int sx = x + dx;
        const int sy = y + dy;
        if (sx < 0 || sy < 0 || sx >= labels.width || sy >= labels.height) {
            return;
        }
        dst.at(x, y) = std::min(dst.at(x, y), dst.at(sx, sy) + peso);
    };

    for (int y = 0; y < labels.height; ++y) {
        for (int x = 0; x < labels.width; ++x) {
            relaxa(x, y, -1, 0, 1.0f);
            relaxa(x, y, 0, -1, 1.0f);
            relaxa(x, y, -1, -1, diagonal);
            relaxa(x, y, 1, -1, diagonal);
        }
    }
    for (int y = labels.height - 1; y >= 0; --y) {
        for (int x = labels.width - 1; x >= 0; --x) {
            relaxa(x, y, 1, 0, 1.0f);
            relaxa(x, y, 0, 1, 1.0f);
            relaxa(x, y, 1, 1, diagonal);
            relaxa(x, y, -1, 1, diagonal);
        }
    }
    return out;
}

}  // namespace

Map<float> distance_transform(MapView<int32_t> labels, bool inside, Metric metric) {
    if (metric != Metric::Euclidean) {
        return chamfer(labels, inside, metric);
    }
    Map<float> out(labels.width, labels.height);
    const MapView<float> dst = out.view();

    for (int y = 0; y < labels.height; ++y) {
        const int32_t* row = labels.row(y);
        float* q = dst.row(y);
        for (int x = 0; x < labels.width; ++x) {
            const bool semente = inside ? (row[x] == 0) : (row[x] != 0);
            q[x] = semente ? 0.0f : infinito;
        }
    }

    const int maior = std::max(labels.width, labels.height);
    std::vector<float> f(static_cast<std::size_t>(maior));
    std::vector<float> d(static_cast<std::size_t>(maior));
    std::vector<int> v(static_cast<std::size_t>(maior));
    std::vector<float> z(static_cast<std::size_t>(maior) + 1);

    for (int x = 0; x < labels.width; ++x) {
        for (int y = 0; y < labels.height; ++y) {
            f[static_cast<std::size_t>(y)] = dst.at(x, y);
        }
        distance_1d(f.data(), d.data(), labels.height, v.data(), z.data());
        for (int y = 0; y < labels.height; ++y) {
            dst.at(x, y) = d[static_cast<std::size_t>(y)];
        }
    }
    for (int y = 0; y < labels.height; ++y) {
        float* row = dst.row(y);
        for (int x = 0; x < labels.width; ++x) {
            f[static_cast<std::size_t>(x)] = row[x];
        }
        distance_1d(f.data(), d.data(), labels.width, v.data(), z.data());
        for (int x = 0; x < labels.width; ++x) {
            row[x] = std::sqrt(d[static_cast<std::size_t>(x)]);
        }
    }
    return out;
}

Map<int32_t> reconstruct(MapView<int32_t> marker, MapView<int32_t> mask,
                         const Adjacency& adjacency) {
    Map<int32_t> out(mask.width, mask.height);
    const MapView<int32_t> dst = out.view();
    for (int y = 0; y < mask.height; ++y) {
        int32_t* row = dst.row(y);
        for (int x = 0; x < mask.width; ++x) {
            const int mx = std::min(x, marker.width - 1);
            const int my = std::min(y, marker.height - 1);
            row[x] = std::min(marker.at(mx, my), mask.at(x, y));
        }
    }

    // Duas varreduras cobrem quase tudo; a fila pega o que precisa voltar,
    // como espiral e cabo de U.
    std::vector<int> pending;
    for (int passo = 0; passo < 2; ++passo) {
        const bool frente = passo == 0;
        for (int i = 0; i < mask.height; ++i) {
            const int y = frente ? i : mask.height - 1 - i;
            for (int j = 0; j < mask.width; ++j) {
                const int x = frente ? j : mask.width - 1 - j;
                int32_t melhor = dst.at(x, y);
                for (const Adjacency::Offset& offset : adjacency.offsets) {
                    const bool causal = frente ? (offset.dy < 0 || (offset.dy == 0 && offset.dx < 0))
                                               : (offset.dy > 0 || (offset.dy == 0 && offset.dx > 0));
                    if (!causal) {
                        continue;
                    }
                    const int sx = x + offset.dx;
                    const int sy = y + offset.dy;
                    if (sx < 0 || sy < 0 || sx >= mask.width || sy >= mask.height) {
                        continue;
                    }
                    melhor = std::max(melhor, dst.at(sx, sy));
                }
                dst.at(x, y) = std::min(melhor, mask.at(x, y));
            }
        }
    }

    for (int y = 0; y < mask.height; ++y) {
        for (int x = 0; x < mask.width; ++x) {
            pending.push_back(y * mask.width + x);
        }
    }
    while (!pending.empty()) {
        const int at = pending.back();
        pending.pop_back();
        const int x = at % mask.width;
        const int y = at / mask.width;
        for (const Adjacency::Offset& offset : adjacency.offsets) {
            const int sx = x + offset.dx;
            const int sy = y + offset.dy;
            if (sx < 0 || sy < 0 || sx >= mask.width || sy >= mask.height) {
                continue;
            }
            const int32_t alvo = std::min(dst.at(x, y), mask.at(sx, sy));
            if (dst.at(sx, sy) < alvo) {
                dst.at(sx, sy) = alvo;
                pending.push_back(sy * mask.width + sx);
            }
        }
    }
    return out;
}

Map<int32_t> fill_holes(MapView<int32_t> labels, const Adjacency& adjacency) {
    // Marcador: só a borda do complemento. Ele cresce pelo fundo conectado à
    // margem, e o fundo que não alcança é buraco.
    Map<int32_t> complemento(labels.width, labels.height);
    Map<int32_t> marcador(labels.width, labels.height);
    marcador.fill(0);
    for (int y = 0; y < labels.height; ++y) {
        for (int x = 0; x < labels.width; ++x) {
            const int32_t livre = labels.at(x, y) == 0 ? 1 : 0;
            complemento.view().at(x, y) = livre;
            if (livre && (x == 0 || y == 0 || x == labels.width - 1 || y == labels.height - 1)) {
                marcador.view().at(x, y) = 1;
            }
        }
    }

    const Map<int32_t> fora = reconstruct(marcador.view(), complemento.view(), adjacency);
    Map<int32_t> out(labels.width, labels.height);
    for (int y = 0; y < labels.height; ++y) {
        for (int x = 0; x < labels.width; ++x) {
            out.view().at(x, y) = fora.view().at(x, y) ? 0 : 1;
        }
    }
    return out;
}

const char* thin_name(Thin kind) {
    switch (kind) {
        case Thin::Thin: return "afinar";
        case Thin::Thicken: return "engrossar";
        case Thin::Skeleton: return "esqueleto";
    }
    return "?";
}

namespace {

// Os dois gabaritos do afinamento e as três rotações de cada, na ordem em que
// o livro aplica.
constexpr int templates[8][9] = {
    {0, 0, 0, -1, 1, -1, 1, 1, 1},  {-1, 0, 0, 1, 1, 0, -1, 1, -1},
    {1, -1, 0, 1, 1, 0, 1, -1, 0},  {-1, 1, -1, 1, 1, 0, -1, 0, 0},
    {1, 1, 1, -1, 1, -1, 0, 0, 0},  {-1, 1, -1, 0, 1, 1, 0, 0, -1},
    {0, -1, 1, 0, 1, 1, 0, -1, 1},  {0, 0, -1, 0, 1, 1, -1, 1, -1},
};

bool matches(MapView<int32_t> labels, int x, int y, const int pattern[9]) {
    for (int j = -1; j <= 1; ++j) {
        for (int i = -1; i <= 1; ++i) {
            const int want = pattern[(j + 1) * 3 + (i + 1)];
            if (want < 0) {
                continue;
            }
            const int sx = x + i;
            const int sy = y + j;
            const int32_t valor = (sx < 0 || sy < 0 || sx >= labels.width || sy >= labels.height)
                                      ? 0
                                      : (labels.at(sx, sy) != 0 ? 1 : 0);
            if (valor != want) {
                return false;
            }
        }
    }
    return true;
}

}  // namespace

Map<int32_t> hit_or_miss(MapView<int32_t> labels, const int pattern[9]) {
    Map<int32_t> out(labels.width, labels.height);
    for (int y = 0; y < labels.height; ++y) {
        for (int x = 0; x < labels.width; ++x) {
            out.view().at(x, y) = matches(labels, x, y, pattern) ? 1 : 0;
        }
    }
    return out;
}

Map<int32_t> thinning(MapView<int32_t> labels, Thin kind, int iterations) {
    Map<int32_t> atual(labels.width, labels.height);
    const bool engrossar = kind == Thin::Thicken;
    for (int y = 0; y < labels.height; ++y) {
        for (int x = 0; x < labels.width; ++x) {
            const int32_t v = labels.at(x, y) != 0 ? 1 : 0;
            atual.view().at(x, y) = engrossar ? 1 - v : v;
        }
    }

    // Cada gabarito casa na imagem inteira antes de qualquer remoção. Apagar
    // durante a varredura faz o gabarito seguinte ver a imagem já comida, e o
    // resultado sai enviesado pro lado em que a varredura anda.
    const int limite = kind == Thin::Skeleton ? 512 : std::max(iterations, 1);
    std::vector<unsigned char> casou(atual.count());
    for (int rodada = 0; rodada < limite; ++rodada) {
        bool mudou = false;
        for (const auto& gabarito : templates) {
            std::fill(casou.begin(), casou.end(), 0);
            for (int y = 0; y < atual.height; ++y) {
                for (int x = 0; x < atual.width; ++x) {
                    if (atual.view().at(x, y) && matches(atual.view(), x, y, gabarito)) {
                        casou[static_cast<std::size_t>(y) * atual.width + x] = 1;
                    }
                }
            }
            for (int y = 0; y < atual.height; ++y) {
                for (int x = 0; x < atual.width; ++x) {
                    if (casou[static_cast<std::size_t>(y) * atual.width + x]) {
                        atual.view().at(x, y) = 0;
                        mudou = true;
                    }
                }
            }
        }
        if (!mudou) {
            break;
        }
    }

    if (engrossar) {
        for (int y = 0; y < atual.height; ++y) {
            for (int x = 0; x < atual.width; ++x) {
                atual.view().at(x, y) = 1 - atual.view().at(x, y);
            }
        }
    }
    return atual;
}
