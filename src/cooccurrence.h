#pragma once

#include <cstdint>
#include <vector>

#include "map.h"

// Matriz de co-ocorrência de níveis de cinza, a GLCM do Haralick. A célula
// (i, j) conta quantas vezes um pixel de nível i tem, no deslocamento dado, um
// vizinho de nível j. Textura lisa enche a diagonal; textura fina espalha pra
// longe dela.

// O deslocamento vai por nome e não por ângulo: "direita" é o pixel ao lado,
// como no livro, e não sobra dúvida de pra que lado o eixo y cresce. Média
// mede nas quatro direções e tira a média das medidas, que é o que o Haralick
// sugere pra textura que não deveria depender de rotação.
enum class Direction { Right, Down, DownRight, DownLeft, Average };

const char* direction_name(Direction direction);

enum class Haralick {
    Contrast,
    Dissimilarity,
    Homogeneity,
    Uniformity,
    Energy,
    Correlation,
    Entropy,
    MaxProbability,
};

constexpr int haralick_count = 8;

const char* haralick_name(Haralick measure);

// Nível de cada pixel em `levels` faixas iguais de [lo, hi]. O topo da faixa
// cai no último nível em vez de abrir um a mais.
Map<int32_t> quantize_levels(MapView<float> scalar, float lo, float hi, int levels);

// A matriz já normalizada, soma 1. Simétrica conta cada par nos dois sentidos,
// que é o que o graycomatrix chama de symmetric e o que torna a medida
// indiferente a trocar o deslocamento pelo oposto. Na Média ela é a média das
// quatro matrizes, que serve pra olhar; as medidas da Média saem de
// haralick_of, que faz a média das medidas e não das matrizes.
struct Cooccurrence {
    int levels = 0;
    std::vector<double> p;

    double at(int i, int j) const { return p[static_cast<std::size_t>(i) * levels + j]; }
};

Cooccurrence cooccurrence(MapView<int32_t> quantized, int levels, Direction direction,
                          int distance, bool symmetric);

// As oito medidas, na ordem do enum. Homogeneidade é a de Haralick e do
// scikit-image, 1 / (1 + (i - j)^2); a do Gonzalez divide por 1 + |i - j|.
// Entropia em bits. Correlação de matriz sem variação nenhuma vale 1, a mesma
// convenção do scikit-image, porque não tem o que medir e zero diria que os
// vizinhos não têm nada a ver um com o outro.
void haralick_of(MapView<int32_t> quantized, int levels, Direction direction, int distance,
                 bool symmetric, double out[haralick_count]);

// Uma medida por pixel, sobre a janela de lado 2 * radius + 1 centrada nele,
// cortada na borda da imagem. Cada janela vê só os pares que cabem inteiros
// dentro dela.
Map<float> texture_map(MapView<int32_t> quantized, int levels, Direction direction, int distance,
                       bool symmetric, int radius, Haralick measure);
