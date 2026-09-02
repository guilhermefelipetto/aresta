#pragma once

#include <vector>

// A_r = {q : d(p,q) <= r}, com o centro de fora. r=1 dá 4 vizinhos, r=1.5 dá 8,
// r=2 dá 12, r=2.3 dá 20, r=2.9 dá o 5x5 inteiro.
//
// A morfologia dobra o próprio pixel de volta na conta, porque elemento
// estruturante inclui a origem; o grafo do marco 7 não vai dobrar, porque aresta
// pra si mesmo não existe.
struct Adjacency {
    struct Offset {
        int dx;
        int dy;
    };

    std::vector<Offset> offsets;
    float radius = 1.0f;
};

Adjacency adjacency_by_radius(float radius);
