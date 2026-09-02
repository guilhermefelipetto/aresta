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
