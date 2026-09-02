#pragma once

#include <cstdint>

#include "adjacency.h"
#include "map.h"

enum class Morph { Erode, Dilate, Open, Close, Gradient, TopHat, BlackHat };

const char* morph_name(Morph operation);

// Morfologia de tom contínuo: erosão é mínimo na vizinhança, dilatação é
// máximo. Sobre um mapa 0/1 isso recai na morfologia binária de sempre.
Map<float> morphology(MapView<float> src, const Adjacency& adjacency, Morph operation);
Map<int32_t> morphology(MapView<int32_t> src, const Adjacency& adjacency, Morph operation);

// Renumera cada componente conexa a partir de 1, mantendo o fundo em 0. Dois
// pixels só entram na mesma componente se tiverem o mesmo rótulo de entrada.
Map<int32_t> connected_components(MapView<int32_t> src, const Adjacency& adjacency, int* count);
