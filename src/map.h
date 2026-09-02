#pragma once

#include <algorithm>
#include <cstddef>
#include <memory>

// Janela sobre um mapa. Não é dona da memória, e o stride é o que permite que
// ela seja um recorte de um mapa maior sem cópia.
template <typename T>
struct MapView {
    T* data = nullptr;
    int width = 0;
    int height = 0;
    int stride = 0;

    bool empty() const { return data == nullptr; }
    T* row(int y) const { return data + static_cast<std::size_t>(y) * stride; }
    T& at(int x, int y) const { return row(y)[x]; }
};

// Um valor por pixel, planar. É onde a IFT vai guardar custo, raiz, predecessor
// e rótulo — mesma geometria da imagem, sem o peso dos quatro canais.
template <typename T>
struct Map {
    int width = 0;
    int height = 0;
    std::unique_ptr<T[]> data;

    Map() = default;
    Map(int w, int h) : width(w), height(h), data(new T[static_cast<std::size_t>(w) * h]) {}

    bool empty() const { return data == nullptr; }
    std::size_t count() const { return static_cast<std::size_t>(width) * height; }

    void fill(const T& value) { std::fill_n(data.get(), count(), value); }

    MapView<T> view() const { return MapView<T>{data.get(), width, height, width}; }

    MapView<T> view(int x, int y, int w, int h) const {
        return MapView<T>{data.get() + static_cast<std::size_t>(y) * width + x, w, h, width};
    }
};
