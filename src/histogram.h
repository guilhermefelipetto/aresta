#pragma once

#include <vector>

#include "value.h"

struct Histogram {
    int bins = 0;
    int channels = 0;
    float lo = 0.0f;
    float hi = 1.0f;
    float peak = 0.0f;
    std::vector<float> counts;

    bool empty() const { return bins == 0 || channels == 0; }
    const float* channel(int index) const {
        return counts.data() + static_cast<std::size_t>(index) * bins;
    }
};

// Cor devolve quatro canais (R, G, B e luminância); escalar e rótulo devolvem
// um. `srgb` só afeta cor, e escolhe entre contar sobre o valor linear guardado
// ou sobre o valor codificado que a tela mostra.
Histogram histogram_of(const Value& value, int bins, bool srgb);

// Nível no domínio do próprio mapa, não índice de faixa.
float otsu_threshold(MapView<float> scalar);
