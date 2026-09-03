#pragma once

#include <cstdint>
#include <vector>

#include "adjacency.h"
#include "map.h"

// O que o filtro precisa saber de cada componente. Perímetro, centroide e
// momentos ficam pro capítulo 11, junto dos descritores.
struct Region {
    int label = 0;
    int area = 0;
    bool touches_border = false;
};

std::vector<Region> measure_regions(MapView<int32_t> labels);

struct ComponentFilter {
    int min_area = 0;
    int max_area = 0;      // 0 é sem teto
    int keep_largest = 0;  // 0 é todos
    bool drop_border = false;
    bool renumber_by_area = true;
    int only_label = 0;    // 0 é todos
};

// Rotula, mede, joga fora o que não passa e renumera. `all` e `kept` são
// opcionais e existem pra janela mostrar a lista sem refazer a conta com regra
// diferente da que a cadeia usou. Em `kept` o rótulo é o de origem, e o rótulo
// novo de cada um é a posição mais um.
Map<int32_t> label_and_filter(MapView<int32_t> source, const Adjacency& adjacency,
                              const ComponentFilter& filter, std::vector<Region>* all,
                              std::vector<Region>* kept);
